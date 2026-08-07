#include "vul_df.h"

#include <string.h>

/* Working buffer for the percentile sorts. Everything that reaches
 * vul_percentile is bounded by the track or the sweep, both 64 wide. */
#define VUL_SORT_MAX 64

/* ------------------------------------------------------------------ *
 * Percentile
 * ------------------------------------------------------------------ */

int16_t vul_percentile(const int16_t* v, uint8_t n, uint8_t k) {
    if(!v || n == 0) return VUL_DBM_INVALID;
    if(n > VUL_SORT_MAX) n = VUL_SORT_MAX;
    if(k >= n) k = (uint8_t)(n - 1);

    int16_t buf[VUL_SORT_MAX];
    memcpy(buf, v, (size_t)n * sizeof(int16_t));

    /* Partial selection sort: only k+1 passes, and k is small in every caller
     * (a quartile of 64 is 16). Insertion sort on the whole array would cost
     * more for no benefit. */
    for(uint8_t i = 0; i <= k; i++) {
        uint8_t min = i;
        for(uint8_t j = (uint8_t)(i + 1); j < n; j++) {
            if(buf[j] < buf[min]) min = j;
        }
        if(min != i) {
            int16_t tmp = buf[i];
            buf[i] = buf[min];
            buf[min] = tmp;
        }
    }
    return buf[k];
}

/* ------------------------------------------------------------------ *
 * Noise floor
 * ------------------------------------------------------------------ */

void vul_floor_init(VulFloor* f, int16_t initial) {
    if(!f) return;
    f->quiet = 0;
    if(initial == VUL_DBM_INVALID) {
        f->floor = VUL_DBM_INVALID;
        f->seeded = false;
    } else {
        f->floor = initial;
        f->seeded = true;
    }
}

void vul_floor_push(VulFloor* f, int16_t dbm) {
    if(!f || dbm == VUL_DBM_INVALID) return;

    if(!f->seeded) {
        f->floor = dbm;
        f->seeded = true;
        f->quiet = 0;
        return;
    }

    if(dbm < f->floor) {
        /* Snap straight down. A quieter reading is proof the floor was wrong. */
        f->floor = dbm;
        f->quiet = 0;
        return;
    }

    /* A sample well above the floor is a signal, not evidence about the noise.
     * Hold: measuring the floor through a carrier is not possible, and trying
     * would walk the estimate up onto the transmitter. */
    if(dbm > (int16_t)(f->floor + VUL_FLOOR_NEAR_DB)) return;

    f->quiet++;
    if(f->quiet >= VUL_FLOOR_RISE_PUSHES) {
        f->quiet = 0;
        /* Creep up, but never past the sample in hand -- drifting above the
         * live reading would manufacture a negative margin out of nothing. */
        if(f->floor < dbm) f->floor++;
    }
}

/* ------------------------------------------------------------------ *
 * Track
 * ------------------------------------------------------------------ */

void vul_track_init(VulTrack* t) {
    if(!t) return;
    memset(t, 0, sizeof(VulTrack));
    for(uint8_t i = 0; i < VUL_TRACK_LEN; i++) t->s[i] = VUL_DBM_INVALID;
    t->head = VUL_TRACK_LEN - 1; /* so the first push lands at index 0 */
}

void vul_track_push(VulTrack* t, int16_t dbm) {
    if(!t) return;
    t->head = (uint8_t)((t->head + 1) % VUL_TRACK_LEN);
    t->s[t->head] = dbm;
    if(t->count < VUL_TRACK_LEN) t->count++;
}

int16_t vul_track_at(const VulTrack* t, uint8_t back) {
    if(!t || back >= t->count) return VUL_DBM_INVALID;
    uint8_t idx = (uint8_t)((t->head + VUL_TRACK_LEN - back) % VUL_TRACK_LEN);
    return t->s[idx];
}

void vul_level(const VulTrack* t, const VulFloor* f, VulLevel* out) {
    if(!out) return;
    memset(out, 0, sizeof(VulLevel));
    out->signal = VUL_DBM_INVALID;
    out->floor = VUL_DBM_INVALID;

    if(!t || t->count < 8) return;

    int16_t buf[VUL_TRACK_LEN];
    uint8_t n = 0;
    for(uint8_t i = 0; i < t->count; i++) {
        int16_t v = vul_track_at(t, i);
        if(v != VUL_DBM_INVALID) buf[n++] = v;
    }
    if(n < 8) return;

    uint8_t k_sig = (uint8_t)(((uint16_t)n * VUL_SIGNAL_NUM) / VUL_SIGNAL_DEN);
    if(k_sig >= n) k_sig = (uint8_t)(n - 1);

    out->signal = vul_percentile(buf, n, k_sig);
    out->spread =
        (int16_t)(vul_percentile(buf, n, (uint8_t)((n * 3) / 4)) - vul_percentile(buf, n, (uint8_t)(n / 4)));
    if(out->spread < 0) out->spread = 0;

    out->floor = (f && f->seeded) ? f->floor : vul_percentile(buf, n, (uint8_t)(n / 4));

    int32_t margin = (int32_t)out->signal - out->floor;
    if(margin < 0) margin = 0;
    out->margin = (int16_t)margin;
    out->valid = true;
}

/* ------------------------------------------------------------------ *
 * Trend
 * ------------------------------------------------------------------ */

void vul_trend(const int16_t* margins, uint8_t n, VulTrendInfo* out) {
    if(!out) return;
    memset(out, 0, sizeof(VulTrendInfo));
    out->trend = VulTrendUnknown;
    out->deadband_x10 = VUL_TREND_DEAD_X10;

    /* Four points is the minimum a slope means anything over; below that the
     * fit is just describing the last thing that happened. */
    if(!margins || n < 4) return;
    if(n > VUL_TREND_LEN) {
        margins += (n - VUL_TREND_LEN); /* keep the newest */
        n = VUL_TREND_LEN;
    }

    int32_t sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    for(uint8_t i = 0; i < n; i++) {
        int32_t x = i;
        int32_t y = margins[i];
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
    }

    int32_t denom = (int32_t)n * sum_xx - sum_x * sum_x;
    if(denom == 0) return;

    int32_t num = (int32_t)n * sum_xy - sum_x * sum_y;

    /* dB per track-sample -> tenths of a dB per second. */
    out->slope_x10 = (int16_t)((num * (int32_t)(VUL_TRACK_HZ * 10)) / denom);

    /* Mean absolute residual, kept in scaled integers so there is no float
     * anywhere in the engine. Everything below is the line
     *     resid_i = y_i - a - b*x_i
     * multiplied through by n*denom to clear both fractions. */
    int64_t acc = 0;
    for(uint8_t i = 0; i < n; i++) {
        int64_t r = (int64_t)n * denom * margins[i] - ((int64_t)sum_y * denom - (int64_t)num * sum_x) -
                    (int64_t)n * num * i;
        acc += (r < 0) ? -r : r;
    }
    int64_t scale = (int64_t)n * n * denom;
    out->resid_x10 = (int16_t)((acc * 10) / scale);

    int32_t dead =
        VUL_TREND_DEAD_X10 + ((int32_t)out->resid_x10 * VUL_TREND_DEAD_PER_RESID) / 10;
    out->deadband_x10 = (int16_t)dead;

    int32_t mag = out->slope_x10 < 0 ? -out->slope_x10 : out->slope_x10;
    if(mag > dead) {
        out->trend = out->slope_x10 > 0 ? VulTrendWarmer : VulTrendColder;
        int32_t conf = ((mag - dead) * 100) / (dead > 0 ? dead : 1);
        out->confidence = (uint8_t)(conf > 100 ? 100 : conf);
    } else {
        out->trend = VulTrendFlat;
        int32_t conf = dead > 0 ? (100 - (mag * 100) / dead) : 100;
        if(conf < 0) conf = 0;
        out->confidence = (uint8_t)conf;
    }
}

/* ------------------------------------------------------------------ *
 * Heat
 * ------------------------------------------------------------------ */

VulHeat vul_heat(int16_t margin) {
    if(margin >= VUL_HEAT_ONTOP_DB) return VulHeatOnTop;
    if(margin >= VUL_HEAT_BURNING_DB) return VulHeatBurning;
    if(margin >= VUL_HEAT_HOT_DB) return VulHeatHot;
    if(margin >= VUL_HEAT_WARM_DB) return VulHeatWarm;
    if(margin >= VUL_HEAT_COOL_DB) return VulHeatCool;
    return VulHeatCold;
}

const char* vul_heat_name(VulHeat h) {
    switch(h) {
    case VulHeatCool:
        return "COOL";
    case VulHeatWarm:
        return "WARM";
    case VulHeatHot:
        return "HOT";
    case VulHeatBurning:
        return "BURNING";
    case VulHeatOnTop:
        return "ON TOP";
    case VulHeatCold:
    default:
        return "COLD";
    }
}

int16_t vul_halvings_x10(int16_t margin_now, int16_t margin_ref) {
    int32_t gain = (int32_t)margin_now - margin_ref;
    return (int16_t)((gain * 10) / VUL_DB_PER_HALVING);
}

/* ------------------------------------------------------------------ *
 * Burst analysis
 * ------------------------------------------------------------------ */

void vul_burst_init(VulBurst* b) {
    if(!b) return;
    memset(b, 0, sizeof(VulBurst));
}

void vul_burst_push(VulBurst* b, int16_t dbm, int16_t floor) {
    if(!b || dbm == VUL_DBM_INVALID || floor == VUL_DBM_INVALID) return;

    b->samples++;
    if(b->since < UINT32_MAX) b->since++;

    int16_t on_th = (int16_t)(floor + VUL_BURST_ON_DB);
    int16_t off_th = (int16_t)(floor + VUL_BURST_OFF_DB);

    if(!b->on) {
        if(dbm >= on_th) {
            b->on = true;
            /* The first edge has nothing to be an interval from. */
            if(b->bursts > 0) {
                uint32_t iv = b->since;
                b->interval[b->iv_head] = (uint16_t)(iv > UINT16_MAX ? UINT16_MAX : iv);
                b->iv_head = (uint8_t)((b->iv_head + 1) % VUL_BURST_INTERVALS);
                if(b->iv_count < VUL_BURST_INTERVALS) b->iv_count++;
            }
            if(b->bursts < UINT16_MAX) b->bursts++;
            b->since = 0;
        }
    } else if(dbm < off_th) {
        b->on = false;
    }

    if(b->on) b->hits++;
}

void vul_burst_result(const VulBurst* b, VulSigInfo* out) {
    if(!out) return;
    memset(out, 0, sizeof(VulSigInfo));
    out->kind = VulSigWaiting;
    if(!b) return;

    out->bursts = b->bursts;
    if(b->samples > 0) {
        uint32_t duty = (b->hits * 100u) / b->samples;
        out->duty_pct = (uint8_t)(duty > 100 ? 100 : duty);
    }

    if(b->samples < VUL_BURST_MIN_SAMPLES) return; /* still VulSigWaiting */

    if(b->hits == 0) {
        out->kind = VulSigNone;
        return;
    }

    if(out->duty_pct >= VUL_DUTY_CONTINUOUS_PCT) {
        out->kind = VulSigContinuous;
        return;
    }

    /* Regularity of the gaps between bursts. Mean absolute deviation rather
     * than a standard deviation, purely to stay in integers -- the ranking it
     * produces is the same and there is no sqrt to pay for. */
    if(b->bursts >= VUL_PERIODIC_MIN_BURSTS && b->iv_count >= 2) {
        uint32_t sum = 0;
        for(uint8_t i = 0; i < b->iv_count; i++) sum += b->interval[i];
        uint32_t mean = sum / b->iv_count;

        if(mean > 0) {
            uint32_t dev = 0;
            for(uint8_t i = 0; i < b->iv_count; i++) {
                uint32_t v = b->interval[i];
                dev += (v > mean) ? (v - mean) : (mean - v);
            }
            dev /= b->iv_count;

            int32_t reg = 100 - (int32_t)((dev * 100u) / mean);
            if(reg < 0) reg = 0;
            out->regularity = (uint8_t)reg;
            out->period_ms = (uint16_t)((mean * 1000u) / VUL_FAST_HZ);

            if(out->regularity >= VUL_PERIODIC_MIN_REGULARITY) {
                out->kind = VulSigPeriodic;
                return;
            }
        }
    }

    out->kind = VulSigIntermittent;
    out->period_ms = 0;
}

const char* vul_sig_name(VulSigKind k) {
    switch(k) {
    case VulSigContinuous:
        return "CONTINUOUS";
    case VulSigPeriodic:
        return "PERIODIC";
    case VulSigIntermittent:
        return "INTERMITTENT";
    case VulSigNone:
        return "QUIET";
    case VulSigWaiting:
    default:
        return "LISTENING";
    }
}

const char* vul_sig_hint(VulSigKind k) {
    switch(k) {
    case VulSigContinuous:
        return "Always on. Analog bug,\nvideo tx or a jammer.";
    case VulSigPeriodic:
        return "Beacons on a clock.\nTracker or telemetry.";
    case VulSigIntermittent:
        return "Irregular traffic. Remote,\nsensor or doorbell.";
    case VulSigNone:
        return "Nothing above the floor\non this frequency.";
    case VulSigWaiting:
    default:
        return "Collecting samples.\nGive it a few seconds.";
    }
}

/* ------------------------------------------------------------------ *
 * Bearing rose
 * ------------------------------------------------------------------ */

void vul_rose_init(VulRose* r) {
    if(!r) return;
    memset(r, 0, sizeof(VulRose));
    for(uint8_t i = 0; i < VUL_ROSE_SECTORS; i++) r->best[i] = VUL_DBM_INVALID;
}

void vul_rose_add(VulRose* r, uint8_t sector, int16_t margin) {
    if(!r || sector >= VUL_ROSE_SECTORS || margin == VUL_DBM_INVALID) return;
    if(r->best[sector] == VUL_DBM_INVALID) {
        r->best[sector] = margin;
        r->visited++;
    } else if(margin > r->best[sector]) {
        r->best[sector] = margin;
    }
}

void vul_rose_result(const VulRose* r, VulRoseResult* out) {
    if(!out) return;
    memset(out, 0, sizeof(VulRoseResult));
    out->best = VUL_DBM_INVALID;
    out->median = VUL_DBM_INVALID;
    if(!r || r->visited == 0) return;

    int16_t vals[VUL_ROSE_SECTORS];
    uint8_t n = 0;
    uint8_t best_sector = 0;
    int16_t best = VUL_DBM_INVALID;

    for(uint8_t i = 0; i < VUL_ROSE_SECTORS; i++) {
        int16_t v = r->best[i];
        if(v == VUL_DBM_INVALID) continue;
        vals[n++] = v;
        if(best == VUL_DBM_INVALID || v > best) {
            best = v;
            best_sector = i;
        }
    }

    out->visited = n;
    out->sector = best_sector;
    out->heading_deg = (uint16_t)(best_sector * (360 / VUL_ROSE_SECTORS));
    out->best = best;
    out->median = vul_percentile(vals, n, (uint8_t)(n / 2));
    out->contrast = (int16_t)(best - out->median);

    /* A flat rose is the honest answer to "which way", not a reason to guess.
     * An omnidirectional whip with nothing shadowing it produces exactly that,
     * and so does a room where the signal arrives off three walls at once. */
    out->conclusive = (n >= VUL_ROSE_MIN_VISITED) && (out->contrast >= VUL_ROSE_MIN_CONTRAST);
}

/* ------------------------------------------------------------------ *
 * Close-in attenuation
 * ------------------------------------------------------------------ */

const uint16_t vul_atten_khz[VUL_ATTEN_STEPS] = {0, 200, 400, 800};

bool vul_should_attenuate(int16_t signal_dbm, uint8_t step) {
    if(signal_dbm == VUL_DBM_INVALID) return false;
    if(step + 1 >= VUL_ATTEN_STEPS) return false;
    return signal_dbm > VUL_ATTEN_PEG_DBM;
}

bool vul_should_relax(int16_t signal_dbm, uint8_t step) {
    if(signal_dbm == VUL_DBM_INVALID) return false;
    if(step == 0) return false;
    return signal_dbm < VUL_ATTEN_RELAX_DBM;
}

/* ------------------------------------------------------------------ *
 * Survey
 * ------------------------------------------------------------------ */

int16_t vul_band_floor(const int16_t* bins, uint8_t n) {
    if(!bins || n == 0) return VUL_DBM_INVALID;
    if(n > VUL_SURVEY_BINS) n = VUL_SURVEY_BINS;

    int16_t valid[VUL_SURVEY_BINS];
    uint8_t m = 0;
    for(uint8_t i = 0; i < n; i++) {
        if(bins[i] != VUL_DBM_INVALID) valid[m++] = bins[i];
    }
    if(m == 0) return VUL_DBM_INVALID;
    return vul_percentile(valid, m, (uint8_t)(m / 4));
}

static bool vul_is_local_max(const int16_t* bins, uint8_t n, uint8_t i) {
    int16_t v = bins[i];
    if(i > 0 && bins[i - 1] != VUL_DBM_INVALID && bins[i - 1] > v) return false;
    if(i + 1 < n && bins[i + 1] != VUL_DBM_INVALID && bins[i + 1] > v) return false;
    return true;
}

uint8_t vul_find_candidates(
    const int16_t* bins,
    uint8_t n,
    int16_t floor,
    int16_t min_margin,
    VulCandidate* out,
    uint8_t max_out) {
    if(!bins || !out || n == 0 || max_out == 0) return 0;
    if(n > VUL_SURVEY_BINS) n = VUL_SURVEY_BINS;
    if(floor == VUL_DBM_INVALID) return 0;

    bool taken[VUL_SURVEY_BINS];
    memset(taken, 0, sizeof(taken));

    uint8_t found = 0;
    while(found < max_out) {
        int16_t best_i = -1;
        for(uint8_t i = 0; i < n; i++) {
            if(taken[i] || bins[i] == VUL_DBM_INVALID) continue;
            if((int16_t)(bins[i] - floor) < min_margin) continue;
            if(!vul_is_local_max(bins, n, i)) continue;
            if(best_i < 0 || bins[i] > bins[best_i]) best_i = (int16_t)i;
        }
        if(best_i < 0) break;

        out[found].bin = (uint8_t)best_i;
        out[found].dbm = bins[best_i];
        out[found].margin = (int16_t)(bins[best_i] - floor);
        found++;

        /* Claim the neighbourhood. One transmitter smeared across the RX
         * filter is one candidate, not five. */
        int16_t lo = (int16_t)(best_i - VUL_CANDIDATE_GUARD);
        int16_t hi = (int16_t)(best_i + VUL_CANDIDATE_GUARD);
        if(lo < 0) lo = 0;
        if(hi > (int16_t)(n - 1)) hi = (int16_t)(n - 1);
        for(int16_t j = lo; j <= hi; j++) taken[j] = true;
    }

    return found;
}

/* 2^(n/10) x1000, for the fractional part of a halving count. The whole part
 * is a shift, which is what keeps this integer-only. */
static const uint16_t vul_pow2_frac_x1000[10] =
    {1000, 1072, 1149, 1231, 1320, 1414, 1516, 1625, 1741, 1866};

uint16_t vul_range_ratio_x10(int16_t halvings_x10) {
    bool nearer = halvings_x10 >= 0;
    int32_t a = nearer ? halvings_x10 : -halvings_x10;
    if(a > 60) a = 60; /* 6 halvings = 64x; past that the RSSI is not credible */

    int32_t r = (int32_t)vul_pow2_frac_x1000[a % 10] << (a / 10); /* x1000 */
    if(!nearer) r = (1000L * 1000L) / r;

    r = (r + 50) / 100; /* -> x10, rounded */
    if(r > 9999) r = 9999;
    if(r < 1) r = 1;
    return (uint16_t)r;
}
