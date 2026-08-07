/* Vulpes direction-finding engine.
 *
 * Everything in here is pure integer arithmetic with no Flipper dependencies,
 * so the whole thing compiles for the host and is checked by test/. That
 * matters more than usual for this app: the radio only ever hands us one
 * number, an RSSI reading, and every claim the product makes -- "you are
 * getting warmer", "that is a beacon, not a bug", "you have halved the
 * distance" -- is this file's interpretation of a stream of those numbers. A
 * screenshot cannot vouch for any of it.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ *
 * Units and sentinels
 * ------------------------------------------------------------------ */

/* Sentinel for "no reading". Chosen below any real CC1101 RSSI (the part
 * bottoms out near -120 dBm) so it always sorts to the bottom. */
#define VUL_DBM_INVALID ((int16_t)-127)

/* ------------------------------------------------------------------ *
 * Sampling geometry
 *
 * The worker samples RSSI fast (VUL_FAST_HZ) because a bug may key up for only
 * a few milliseconds, but it feeds the direction-finding track slowly, one
 * max-held sample per VUL_DECIMATE fast reads. Two reasons:
 *
 *  - Max-hold means a short burst still registers at its real strength instead
 *    of being averaged into the noise by the samples either side of it.
 *  - 25 Hz over a 64-deep track is 2.5 s of history, which is the timescale a
 *    hand actually moves on. A 500 Hz track would show you noise.
 * ------------------------------------------------------------------ */
#define VUL_FAST_HZ 500u
#define VUL_DECIMATE 20u
#define VUL_TRACK_HZ (VUL_FAST_HZ / VUL_DECIMATE) /* 25 Hz */

#define VUL_TRACK_LEN 64 /* 2.56 s of decimated history */
#define VUL_TREND_LEN 16 /* 0.64 s the trend is fitted over */

/* ------------------------------------------------------------------ *
 * Percentile
 * ------------------------------------------------------------------ */

/** k-th smallest of n values, k zero-based, clamped into range.
 *
 * Percentiles rather than means everywhere, because a duty-cycled transmitter
 * has a bimodal distribution: a mean sits in the empty gap between "off" and
 * "on" and describes neither.
 */
int16_t vul_percentile(const int16_t* v, uint8_t n, uint8_t k);

/* ------------------------------------------------------------------ *
 * Noise floor
 *
 * The floor cannot be a percentile of the same window the signal comes from:
 * stand still on top of a continuous carrier and every sample in the window is
 * the carrier, so a low percentile would report the carrier as the floor and
 * the margin would collapse to zero exactly when you had found the thing.
 *
 * So the floor is tracked over time instead: it drops instantly to any new
 * minimum and creeps back up slowly, which means it settles to the quietest
 * thing this frequency has done recently. Survey seeds it with the band floor
 * measured across 64 frequencies, which is a genuinely independent estimate.
 * ------------------------------------------------------------------ */

/* One dB of upward drift per this many pushes (~2.5 s at the track rate). */
#define VUL_FLOOR_RISE_PUSHES 64

/* The floor only drifts upward while the samples are near it. Once a sample is
 * this far above, something is transmitting, and you cannot measure a noise
 * floor through a carrier sitting on top of it -- so the estimate holds rather
 * than climbing toward the signal.
 *
 * Without this rule the app breaks in the one place it must not: stand still
 * beside a continuous bug and the floor would creep up to meet it, the margin
 * would collapse, and the screen would cool from BURNING to COLD while you
 * stood on top of the transmitter.
 *
 * The cost of holding is that a floor seeded too low stays too low, inflating
 * the margin by a constant. That is the cheaper mistake by far: every reading
 * the hunt actually steers by -- the trend, the halvings, the rose contrast --
 * is a difference, and a constant offset cancels out of all three.
 *
 * Deliberately equal to VUL_CANDIDATE_MIN_DB: the same margin that makes the
 * survey call something a signal is the one that stops the floor tracking it.
 */
#define VUL_FLOOR_NEAR_DB 10

typedef struct {
    int16_t floor;
    uint16_t quiet; /* pushes near the floor since it last moved */
    bool seeded;
} VulFloor;

void vul_floor_init(VulFloor* f, int16_t initial);
void vul_floor_push(VulFloor* f, int16_t dbm);

/* ------------------------------------------------------------------ *
 * Sample track + level estimate
 * ------------------------------------------------------------------ */

typedef struct {
    int16_t s[VUL_TRACK_LEN];
    uint8_t head; /* index of the newest sample */
    uint8_t count;
} VulTrack;

void vul_track_init(VulTrack* t);
void vul_track_push(VulTrack* t, int16_t dbm);
/** `back` = 0 is the newest sample. Returns VUL_DBM_INVALID past the end. */
int16_t vul_track_at(const VulTrack* t, uint8_t back);

typedef struct {
    int16_t signal; /* upper-percentile level, dBm */
    int16_t floor; /* from VulFloor, dBm */
    int16_t margin; /* signal - floor, never negative */
    int16_t spread; /* p75 - p25: how noisy this window is, dB */
    bool valid; /* enough samples to mean anything */
} VulLevel;

/* The signal estimate is the 7/8th percentile of the window: high enough to
 * ignore the gaps in a bursty transmission, low enough that one spike of
 * interference cannot define the reading the way a plain max would. */
#define VUL_SIGNAL_NUM 7
#define VUL_SIGNAL_DEN 8

void vul_level(const VulTrack* t, const VulFloor* f, VulLevel* out);

/* ------------------------------------------------------------------ *
 * Trend -- the "getting warmer" judgement
 *
 * Not a difference between two samples; that is pure noise. A least-squares
 * slope over the last VUL_TREND_LEN margins, with a deadband that widens when
 * the signal itself is noisy. On a jittery signal the tool should refuse to
 * claim a direction rather than flicker between warmer and colder, which is
 * what a naive delta does and why naive DF tools are useless.
 * ------------------------------------------------------------------ */

typedef enum {
    VulTrendUnknown, /* not enough history yet */
    VulTrendColder,
    VulTrendFlat,
    VulTrendWarmer,
} VulTrend;

/* Base deadband, in tenths of a dB per second. */
#define VUL_TREND_DEAD_X10 10
/* How much each dB of residual widens the deadband, in tenths of a dB per
 * second. The window is VUL_TREND_LEN/VUL_TRACK_HZ = 0.64 s long, so a scatter
 * of 1 dB about the fitted line can fake a slope of roughly 1/0.64 = 1.5 dB/s.
 * The tool must beat its own noise before it claims a direction. */
#define VUL_TREND_DEAD_PER_RESID 15

typedef struct {
    VulTrend trend;
    int16_t slope_x10; /* tenths of a dB per second, signed */
    int16_t resid_x10; /* mean absolute residual from the fit, tenths of a dB */
    int16_t deadband_x10; /* what the slope had to beat to count */
    uint8_t confidence; /* 0..100, how far past the deadband it got */
} VulTrendInfo;

/** Fit a trend over `n` margins, newest last. The deadband is derived from the
 * fit's own residual, so noisy data raises the bar instead of producing a
 * confident answer. */
void vul_trend(const int16_t* margins, uint8_t n, VulTrendInfo* out);

/* ------------------------------------------------------------------ *
 * Heat ladder -- margin in dB above the floor, as a word
 * ------------------------------------------------------------------ */

typedef enum {
    VulHeatCold,
    VulHeatCool,
    VulHeatWarm,
    VulHeatHot,
    VulHeatBurning,
    VulHeatOnTop,
    VulHeatCount,
} VulHeat;

#define VUL_HEAT_COOL_DB 6
#define VUL_HEAT_WARM_DB 14
#define VUL_HEAT_HOT_DB 24
#define VUL_HEAT_BURNING_DB 36
#define VUL_HEAT_ONTOP_DB 50

VulHeat vul_heat(int16_t margin);
const char* vul_heat_name(VulHeat h);

/* ------------------------------------------------------------------ *
 * Distance proxy
 *
 * Free-space loss is 6 dB per doubling of distance, so a 6 dB gain means you
 * are half as far away as you were. That is a real, defensible relative
 * statement, and it is all RSSI can support -- there is no way to turn a
 * received level into metres without knowing the transmit power, which for a
 * bug you have not found yet you do not.
 *
 * Indoors the exponent is worse than free space, so a real halving usually
 * takes more than 6 dB. The number is therefore optimistic and the app says so.
 * ------------------------------------------------------------------ */
#define VUL_DB_PER_HALVING 6

/** Halvings of distance since the mark, in tenths. Positive = closer. */
int16_t vul_halvings_x10(int16_t margin_now, int16_t margin_ref);

/* ------------------------------------------------------------------ *
 * Burst / duty analysis -- what kind of thing is transmitting
 *
 * Fed from every fast sample, not the decimated track, and accumulated over
 * seconds so a beacon that speaks once every few seconds is still caught.
 * Distinguishing "continuous carrier" from "regular beacon" from "occasional
 * traffic" is what separates a bug from a neighbour's weather station, and it
 * is the difference between this being an instrument and being a noise meter.
 * ------------------------------------------------------------------ */

#define VUL_BURST_INTERVALS 8
/* Schmitt trigger, in dB above floor: rise at ON, fall at OFF. A single
 * threshold would chatter on every sample that grazed it and multiply one
 * burst into twenty. */
#define VUL_BURST_ON_DB 8
#define VUL_BURST_OFF_DB 5
/* Below this many samples, no classification is offered at all. */
#define VUL_BURST_MIN_SAMPLES 200

typedef struct {
    uint32_t samples;
    uint32_t hits; /* samples above the trigger */
    uint16_t bursts; /* rising edges seen */
    bool on; /* inside a burst right now */
    uint32_t since; /* fast samples since the last rising edge */
    uint16_t interval[VUL_BURST_INTERVALS]; /* fast samples between edges */
    uint8_t iv_head;
    uint8_t iv_count;
} VulBurst;

typedef enum {
    VulSigNone, /* nothing above the floor worth calling a signal */
    VulSigContinuous, /* always on: analog bug, video tx, jammer */
    VulSigPeriodic, /* regular bursts: tracker, telemetry beacon */
    VulSigIntermittent, /* irregular traffic: remote, sensor, doorbell */
    VulSigWaiting, /* not enough samples yet to say */
} VulSigKind;

#define VUL_DUTY_CONTINUOUS_PCT 90
#define VUL_PERIODIC_MIN_BURSTS 3
#define VUL_PERIODIC_MIN_REGULARITY 70

typedef struct {
    VulSigKind kind;
    uint8_t duty_pct;
    uint16_t period_ms; /* mean gap between bursts, 0 if not periodic */
    uint8_t regularity; /* 0..100, how even those gaps were */
    uint16_t bursts;
} VulSigInfo;

void vul_burst_init(VulBurst* b);
void vul_burst_push(VulBurst* b, int16_t dbm, int16_t floor);
void vul_burst_result(const VulBurst* b, VulSigInfo* out);
const char* vul_sig_name(VulSigKind k);
/** One line on what this pattern usually means in the field. */
const char* vul_sig_hint(VulSigKind k);

/* ------------------------------------------------------------------ *
 * Bearing rose
 *
 * A whip antenna is omnidirectional, so a single reading carries no direction.
 * The technique that does work is to make your own body the shield: hold the
 * Flipper against your chest and turn slowly through a full circle. Your torso
 * attenuates whatever is behind it, so the peak points at the transmitter.
 *
 * There is no magnetometer, so sectors come from elapsed time -- turn at a
 * steady rate and sector k is heading k * 30 degrees from where you started.
 * The result reports its own contrast and refuses to name a direction when the
 * rose is too flat to support one.
 * ------------------------------------------------------------------ */

#define VUL_ROSE_SECTORS 12 /* 30 degrees each */
#define VUL_ROSE_MIN_CONTRAST 6 /* dB of peak-to-median before we commit */
#define VUL_ROSE_MIN_VISITED 9 /* sectors that must hold data */

typedef struct {
    int16_t best[VUL_ROSE_SECTORS]; /* max margin seen in each sector */
    uint8_t visited;
} VulRose;

typedef struct {
    uint8_t sector;
    uint16_t heading_deg;
    int16_t best;
    int16_t median;
    int16_t contrast; /* best - median */
    uint8_t visited;
    bool conclusive;
} VulRoseResult;

void vul_rose_init(VulRose* r);
void vul_rose_add(VulRose* r, uint8_t sector, int16_t margin);
void vul_rose_result(const VulRose* r, VulRoseResult* out);

/* ------------------------------------------------------------------ *
 * Close-in attenuation
 *
 * The CC1101's RSSI saturates near -10 dBm. Walk inside a couple of metres of
 * a transmitter and the reading pegs, the margin stops moving and the hunt
 * goes blind exactly where it should be sharpest.
 *
 * Fox-hunters solve this with an offset attenuator: deliberately tune off
 * frequency so the receiver's own filter skirt knocks the signal down and the
 * top of the scale is usable again. Same trick here, in software, free.
 * ------------------------------------------------------------------ */

#define VUL_ATTEN_STEPS 4
/* Offsets applied per step, in kHz. Step 0 is on frequency. */
extern const uint16_t vul_atten_khz[VUL_ATTEN_STEPS];

#define VUL_ATTEN_PEG_DBM (-30) /* above this, back off a step */
#define VUL_ATTEN_RELAX_DBM (-60) /* below this, come back down a step */

bool vul_should_attenuate(int16_t signal_dbm, uint8_t step);
bool vul_should_relax(int16_t signal_dbm, uint8_t step);

/* ------------------------------------------------------------------ *
 * Survey -- picking candidates out of a swept band
 * ------------------------------------------------------------------ */

#define VUL_SURVEY_BINS 64
#define VUL_MAX_CANDIDATES 6
/* A bin must stand this far above the band floor to be worth chasing. */
#define VUL_CANDIDATE_MIN_DB 10
/* Bins either side of an accepted peak that get suppressed, so one carrier
 * spread across the RX filter does not fill the list with itself. */
#define VUL_CANDIDATE_GUARD 2

typedef struct {
    uint8_t bin;
    int16_t dbm;
    int16_t margin;
} VulCandidate;

/** Lower quartile across the sweep: the band's own noise floor. Robust because
 * most of a band is empty, so loud carriers cannot drag a quartile. */
int16_t vul_band_floor(const int16_t* bins, uint8_t n);

/** Local maxima above `floor + min_margin`, strongest first. Returns count. */
uint8_t vul_find_candidates(
    const int16_t* bins,
    uint8_t n,
    int16_t floor,
    int16_t min_margin,
    VulCandidate* out,
    uint8_t max_out);

/** Distance ratio since the mark, x10: 35 reads as "3.5x closer". Values under
 * 10 mean you have moved away. Derived from vul_halvings_x10, so it inherits
 * the same free-space assumption and the same optimism indoors. */
uint16_t vul_range_ratio_x10(int16_t halvings_x10);
