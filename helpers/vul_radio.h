/* The CC1101 side of Vulpes: one worker thread that runs in either of two
 * modes, and publishes a mutex-guarded snapshot the GUI copies out.
 *
 *   SURVEY -- sweep a band in VUL_SURVEY_BINS steps, max-holding across passes
 *             so a transmitter that only keys up occasionally still gets seen,
 *             and pull candidates out of the result.
 *   HUNT   -- sit on one frequency and sample fast, feeding the direction
 *             finding engine in helpers/vul_df.h.
 *
 * Listen-only. Vulpes never transmits.
 */
#pragma once

#include "vul_df.h"

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ *
 * Bands
 *
 * The first three are the full ranges the Flipper's CC1101 will tune, for
 * finding something when you have no idea where it lives. The last three are
 * the ISM allocations themselves, for a fast focused check -- 64 bins across
 * 1.7 MHz resolves far better than 64 bins across 77 MHz.
 * ------------------------------------------------------------------ */

#define VUL_BAND_COUNT 6

typedef struct {
    uint32_t start;
    uint32_t end;
    const char* label;
    const char* note;
} VulBand;

extern const VulBand vul_bands[VUL_BAND_COUNT];

/** Centre frequency of a survey bin, in Hz. */
uint32_t vul_bin_freq(uint8_t band, uint8_t bin);

/* ------------------------------------------------------------------ *
 * Snapshots
 * ------------------------------------------------------------------ */

typedef struct {
    bool running;
    bool valid; /* false if the CC1101 could not be brought up */
    uint8_t band;

    int16_t bins[VUL_SURVEY_BINS]; /* the sweep just finished */
    int16_t hold[VUL_SURVEY_BINS]; /* max-hold across every sweep so far */
    int16_t floor; /* band noise floor, lower quartile of the hold */
    uint32_t sweeps;

    VulCandidate cand[VUL_MAX_CANDIDATES];
    uint8_t cand_count;
} VulSurveySnapshot;

/* Width of the on-screen trace, and the dB range it spans. */
#define VUL_TRACE_LEN 64
#define VUL_TRACE_RANGE_DB 60

typedef struct {
    bool running;
    bool valid;

    uint32_t frequency; /* the frequency being hunted, before any offset */
    uint32_t tuned; /* where the radio actually sits right now */
    int16_t rssi; /* newest raw fast sample, dBm */

    VulLevel level;
    VulTrendInfo trend;
    VulSigInfo sig;

    int16_t peak_margin; /* best margin since the last reset */
    int16_t mark_margin; /* margin when the user dropped a mark */
    bool have_mark;

    uint8_t atten_step;
    uint16_t atten_khz; /* offset actually applied, 0 if none was possible */
    bool atten_auto;
    uint32_t atten_changed_at; /* update counter when the step last moved */

    uint8_t trace[VUL_TRACE_LEN]; /* 0..100, newest at trace_head */
    uint8_t trace_head;

    uint32_t updates; /* decimated ticks published */
    uint16_t sample_hz; /* measured fast-loop rate */
} VulHuntSnapshot;

/* ------------------------------------------------------------------ */

typedef struct VulRadio VulRadio;

VulRadio* vul_radio_alloc(void);
void vul_radio_free(VulRadio* r);

/** Sweep `band` looking for anything worth chasing. */
void vul_radio_survey_start(VulRadio* r, uint8_t band);

/** Sit on `frequency` and hunt. `seed_floor` is the band noise floor measured
 * by the survey, or VUL_DBM_INVALID to learn one from scratch -- the survey's
 * is much better, because it comes from 64 different frequencies rather than
 * from the one the transmitter is sitting on. */
void vul_radio_hunt_start(VulRadio* r, uint32_t frequency, int16_t seed_floor);

void vul_radio_stop(VulRadio* r);

void vul_radio_survey_get(VulRadio* r, VulSurveySnapshot* out);
void vul_radio_hunt_get(VulRadio* r, VulHuntSnapshot* out);

/** Clear the peak-hold and the survey's max-hold. */
void vul_radio_reset_peak(VulRadio* r);
/** Restart the burst statistics, e.g. after moving to a new room. */
void vul_radio_reset_stats(VulRadio* r);
/** Remember the current margin, so the display can report progress from it. */
void vul_radio_mark(VulRadio* r);

/** Retune the survey to another band without restarting the radio. The
 * max-hold is cleared, because holds from two different bands are not
 * comparable. */
void vul_radio_set_band(VulRadio* r, uint8_t band);

void vul_radio_set_atten_auto(VulRadio* r, bool on);
/** Step the manual attenuator. Ignored while auto is on. */
void vul_radio_set_atten_step(VulRadio* r, uint8_t step);
