#include "vul_radio.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h> /* FuriHalSubGhzPreset */
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

#include <string.h>

#define TAG "Vulpes"

#define VUL_WORKER_STACK (2 * 1024)

/* Survey dwell. 64 bins at this settle is roughly a 40 ms sweep, so the
 * max-hold gets ~25 looks per second at every frequency in the band -- which
 * is what gives a beacon keyed for 40 ms once a second a real chance of being
 * caught rather than swept past. */
#define VUL_SURVEY_SETTLE_US 500

/* Hunt loop period. Nominally VUL_FAST_HZ; the real rate is measured and
 * reported, because the burst period the app prints depends on it. */
#define VUL_HUNT_PERIOD_US (1000000u / VUL_FAST_HZ)

/* Decimated updates that must pass before the attenuator may move again. The
 * track has to refill at the new level before the trend means anything. */
#define VUL_ATTEN_COOLDOWN (VUL_TRACK_HZ * 2)

/* The narrowest stock RX filter, 270 kHz. Selectivity is the whole point:
 * a hunt wants the transmitter it locked on to, not its neighbour. */
#define VUL_PRESET FuriHalSubGhzPresetOok270Async

const VulBand vul_bands[VUL_BAND_COUNT] = {
    /* Wide sweeps, for when you have no idea what you are looking for. */
    {300000000, 348000000, "300-348", "315 region"},
    {387000000, 464000000, "387-464", "433 region"},
    {779000000, 928000000, "779-928", "868/915 region"},
    /* The ISM allocations themselves, for a fast focused check. */
    {433050000, 434790000, "433 ISM", "remotes+sensors"},
    {868000000, 868600000, "868 ISM", "EU telemetry"},
    {902000000, 928000000, "915 ISM", "US telemetry"},
};

uint32_t vul_bin_freq(uint8_t band, uint8_t bin) {
    if(band >= VUL_BAND_COUNT) band = 0;
    if(bin >= VUL_SURVEY_BINS) bin = VUL_SURVEY_BINS - 1;
    const VulBand* b = &vul_bands[band];
    uint64_t span = (uint64_t)(b->end - b->start);
    return b->start + (uint32_t)((span * bin) / (VUL_SURVEY_BINS - 1));
}

/* ------------------------------------------------------------------ */

struct VulRadio {
    FuriThread* thread;
    FuriMutex* mutex; /* guards the request block and both snapshots */
    volatile bool running;
    bool survey_mode;

    /* requests, consumed by the worker */
    uint8_t req_band;
    uint32_t req_freq;
    int16_t req_seed_floor;
    bool req_reset_peak;
    bool req_reset_stats;
    bool req_mark;
    bool req_atten_auto;
    uint8_t req_atten_step;

    VulSurveySnapshot survey;
    VulHuntSnapshot hunt;
};

static uint8_t vul_trace_level(int16_t margin) {
    if(margin <= 0) return 0;
    if(margin >= VUL_TRACE_RANGE_DB) return 100;
    return (uint8_t)(((int32_t)margin * 100) / VUL_TRACE_RANGE_DB);
}

/* ------------------------------------------------------------------ *
 * Survey
 * ------------------------------------------------------------------ */

static void vul_survey_thread(VulRadio* r, const SubGhzDevice* device) {
    int16_t bins[VUL_SURVEY_BINS];
    int16_t hold[VUL_SURVEY_BINS];
    VulCandidate cand[VUL_MAX_CANDIDATES];

    for(uint8_t i = 0; i < VUL_SURVEY_BINS; i++) hold[i] = VUL_DBM_INVALID;

    furi_mutex_acquire(r->mutex, FuriWaitForever);
    uint8_t band = r->req_band;
    r->survey.band = band;
    r->survey.sweeps = 0;
    furi_mutex_release(r->mutex);

    uint32_t sweeps = 0;

    while(r->running) {
        furi_mutex_acquire(r->mutex, FuriWaitForever);
        bool clear = r->req_reset_peak;
        r->req_reset_peak = false;
        if(r->req_band != band) {
            band = r->req_band;
            clear = true;
        }
        furi_mutex_release(r->mutex);

        if(clear) {
            for(uint8_t i = 0; i < VUL_SURVEY_BINS; i++) hold[i] = VUL_DBM_INVALID;
            sweeps = 0;
        }

        for(uint8_t bin = 0; bin < VUL_SURVEY_BINS && r->running; bin++) {
            uint32_t freq = vul_bin_freq(band, bin);
            if(!subghz_devices_is_frequency_valid(device, freq)) {
                bins[bin] = VUL_DBM_INVALID;
                continue;
            }
            subghz_devices_idle(device);
            subghz_devices_set_frequency(device, freq);
            subghz_devices_flush_rx(device);
            subghz_devices_set_rx(device);
            furi_delay_us(VUL_SURVEY_SETTLE_US);
            bins[bin] = (int16_t)subghz_devices_get_rssi(device);
        }
        if(!r->running) break;

        /* Max-hold is what makes the survey work on bursty transmitters: a
         * beacon that speaks for 40 ms will be missed by most sweeps and
         * caught by one, and one is enough. */
        for(uint8_t i = 0; i < VUL_SURVEY_BINS; i++) {
            if(bins[i] == VUL_DBM_INVALID) continue;
            if(hold[i] == VUL_DBM_INVALID || bins[i] > hold[i]) hold[i] = bins[i];
        }
        sweeps++;

        int16_t floor = vul_band_floor(hold, VUL_SURVEY_BINS);
        uint8_t n = vul_find_candidates(
            hold, VUL_SURVEY_BINS, floor, VUL_CANDIDATE_MIN_DB, cand, VUL_MAX_CANDIDATES);

        furi_mutex_acquire(r->mutex, FuriWaitForever);
        VulSurveySnapshot* s = &r->survey;
        s->band = band;
        memcpy(s->bins, bins, sizeof(bins));
        memcpy(s->hold, hold, sizeof(hold));
        s->floor = floor;
        s->sweeps = sweeps;
        memcpy(s->cand, cand, sizeof(cand));
        s->cand_count = n;
        furi_mutex_release(r->mutex);
    }
}

/* ------------------------------------------------------------------ *
 * Hunt
 * ------------------------------------------------------------------ */

/* Tune off frequency so the receiver's own filter skirt knocks the signal
 * down. Up if the band allows it, down if not, and honest about applying
 * nothing if neither offset is legal. */
static uint32_t
    vul_offset_freq(const SubGhzDevice* device, uint32_t base, uint16_t khz, uint16_t* applied) {
    if(khz == 0) {
        *applied = 0;
        return base;
    }
    uint32_t off = (uint32_t)khz * 1000u;

    uint32_t up = base + off;
    if(subghz_devices_is_frequency_valid(device, up)) {
        *applied = khz;
        return up;
    }
    if(base > off) {
        uint32_t down = base - off;
        if(subghz_devices_is_frequency_valid(device, down)) {
            *applied = khz;
            return down;
        }
    }
    *applied = 0;
    return base;
}

static void vul_hunt_thread(VulRadio* r, const SubGhzDevice* device) {
    VulTrack track;
    VulFloor floor;
    VulBurst burst;
    int16_t margins[VUL_TREND_LEN];
    uint8_t margin_count = 0;

    furi_mutex_acquire(r->mutex, FuriWaitForever);
    uint32_t base_freq = r->req_freq;
    int16_t seed = r->req_seed_floor;
    bool atten_auto = r->req_atten_auto;
    uint8_t atten_step = r->req_atten_step;
    r->hunt.frequency = base_freq;
    furi_mutex_release(r->mutex);

    vul_track_init(&track);
    vul_floor_init(&floor, seed);
    vul_burst_init(&burst);

    uint16_t applied_khz = 0;
    uint32_t tuned = vul_offset_freq(device, base_freq, vul_atten_khz[atten_step], &applied_khz);

    subghz_devices_idle(device);
    subghz_devices_set_frequency(device, tuned);
    subghz_devices_flush_rx(device);
    subghz_devices_set_rx(device);
    furi_delay_us(2000);

    uint8_t decim = 0;
    int16_t dmax = VUL_DBM_INVALID;
    int16_t peak_margin = 0;
    int16_t mark_margin = 0;
    bool have_mark = false;
    uint32_t updates = 0;
    uint32_t atten_changed_at = 0;
    uint8_t trace[VUL_TRACE_LEN];
    uint8_t trace_head = 0;
    memset(trace, 0, sizeof(trace));

    /* Measured loop rate. The burst period printed on screen is derived from
     * it, so it is measured rather than assumed. */
    uint16_t sample_hz = VUL_FAST_HZ;
    uint32_t rate_mark = furi_get_tick();
    uint32_t rate_count = 0;

    while(r->running) {
        int16_t rssi = (int16_t)subghz_devices_get_rssi(device);

        vul_burst_push(&burst, rssi, floor.floor);
        if(dmax == VUL_DBM_INVALID || rssi > dmax) dmax = rssi;

        rate_count++;
        uint32_t now = furi_get_tick();
        uint32_t elapsed = now - rate_mark;
        if(elapsed >= furi_kernel_get_tick_frequency()) {
            uint32_t hz = (rate_count * furi_kernel_get_tick_frequency()) / elapsed;
            sample_hz = (uint16_t)(hz > 65535u ? 65535u : hz);
            rate_mark = now;
            rate_count = 0;
        }

        if(++decim < VUL_DECIMATE) {
            furi_delay_us(VUL_HUNT_PERIOD_US);
            continue;
        }
        decim = 0;

        /* ---- one decimated update ---- */
        vul_track_push(&track, dmax);
        vul_floor_push(&floor, dmax);
        dmax = VUL_DBM_INVALID;

        VulLevel level;
        vul_level(&track, &floor, &level);

        if(level.valid) {
            if(margin_count < VUL_TREND_LEN) {
                margins[margin_count++] = level.margin;
            } else {
                memmove(margins, margins + 1, sizeof(int16_t) * (VUL_TREND_LEN - 1));
                margins[VUL_TREND_LEN - 1] = level.margin;
            }
            if(level.margin > peak_margin) peak_margin = level.margin;
        }

        VulTrendInfo trend;
        vul_trend(margins, margin_count, &trend);

        VulSigInfo sig;
        vul_burst_result(&burst, &sig);
        /* Correct the printed period for the loop rate we actually achieved
         * rather than the one the engine assumes. */
        if(sig.period_ms && sample_hz) {
            sig.period_ms = (uint16_t)(((uint32_t)sig.period_ms * VUL_FAST_HZ) / sample_hz);
        }

        updates++;
        trace_head = (uint8_t)((trace_head + 1) % VUL_TRACE_LEN);
        trace[trace_head] = vul_trace_level(level.margin);

        /* ---- requests ---- */
        furi_mutex_acquire(r->mutex, FuriWaitForever);
        bool reset_peak = r->req_reset_peak;
        bool reset_stats = r->req_reset_stats;
        bool mark = r->req_mark;
        bool want_auto = r->req_atten_auto;
        uint8_t want_step = r->req_atten_step;
        r->req_reset_peak = false;
        r->req_reset_stats = false;
        r->req_mark = false;
        furi_mutex_release(r->mutex);

        if(reset_peak) peak_margin = level.valid ? level.margin : 0;
        if(reset_stats) vul_burst_init(&burst);
        if(mark && level.valid) {
            mark_margin = level.margin;
            have_mark = true;
        }
        atten_auto = want_auto;

        /* ---- attenuation ---- */
        uint8_t next_step = atten_step;
        if(atten_auto) {
            if(updates - atten_changed_at >= VUL_ATTEN_COOLDOWN && level.valid) {
                if(vul_should_attenuate(level.signal, atten_step)) {
                    next_step = (uint8_t)(atten_step + 1);
                } else if(vul_should_relax(level.signal, atten_step)) {
                    next_step = (uint8_t)(atten_step - 1);
                }
            }
        } else if(want_step != atten_step && want_step < VUL_ATTEN_STEPS) {
            next_step = want_step;
        }

        if(next_step != atten_step) {
            atten_step = next_step;
            tuned = vul_offset_freq(device, base_freq, vul_atten_khz[atten_step], &applied_khz);
            subghz_devices_idle(device);
            subghz_devices_set_frequency(device, tuned);
            subghz_devices_flush_rx(device);
            subghz_devices_set_rx(device);
            furi_delay_us(2000);

            /* Everything measured at the old gain is now a lie. Left in place,
             * the level jump would read as a violent COLDER at exactly the
             * moment the operator got closest -- so the history goes. */
            vul_track_init(&track);
            vul_floor_init(&floor, VUL_DBM_INVALID);
            margin_count = 0;
            peak_margin = 0;
            have_mark = false;
            atten_changed_at = updates;
        }

        /* ---- publish ---- */
        furi_mutex_acquire(r->mutex, FuriWaitForever);
        VulHuntSnapshot* h = &r->hunt;
        h->frequency = base_freq;
        h->tuned = tuned;
        h->rssi = rssi;
        h->level = level;
        h->trend = trend;
        h->sig = sig;
        h->peak_margin = peak_margin;
        h->mark_margin = mark_margin;
        h->have_mark = have_mark;
        h->atten_step = atten_step;
        h->atten_khz = applied_khz;
        h->atten_auto = atten_auto;
        h->atten_changed_at = atten_changed_at;
        memcpy(h->trace, trace, sizeof(trace));
        h->trace_head = trace_head;
        h->updates = updates;
        h->sample_hz = sample_hz;
        furi_mutex_release(r->mutex);

        furi_delay_us(VUL_HUNT_PERIOD_US);
    }

    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_atten_step = atten_step;
    furi_mutex_release(r->mutex);
}

/* ------------------------------------------------------------------ *
 * Thread entry
 * ------------------------------------------------------------------ */

static int32_t vul_radio_thread(void* context) {
    VulRadio* r = context;

    subghz_devices_init();
    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    bool ok = device != NULL;

    if(ok) {
        subghz_devices_begin(device);
        subghz_devices_reset(device);
        subghz_devices_load_preset(device, VUL_PRESET, NULL);
        subghz_devices_set_rx(device);
    }

    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->survey.running = true;
    r->survey.valid = ok;
    r->hunt.running = true;
    r->hunt.valid = ok;
    furi_mutex_release(r->mutex);

    if(!ok) {
        FURI_LOG_E(TAG, "no CC1101");
        while(r->running) furi_delay_ms(50);
    } else if(r->survey_mode) {
        vul_survey_thread(r, device);
    } else {
        vul_hunt_thread(r, device);
    }

    if(ok) {
        subghz_devices_idle(device);
        subghz_devices_sleep(device);
        subghz_devices_end(device);
    }
    subghz_devices_deinit();

    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->survey.running = false;
    r->hunt.running = false;
    furi_mutex_release(r->mutex);
    return 0;
}

/* ------------------------------------------------------------------ *
 * API
 * ------------------------------------------------------------------ */

VulRadio* vul_radio_alloc(void) {
    VulRadio* r = malloc(sizeof(VulRadio));
    memset(r, 0, sizeof(VulRadio));
    r->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    r->req_band = 3; /* 433 ISM: where most of the interesting traffic is */
    r->req_seed_floor = VUL_DBM_INVALID;
    r->req_atten_auto = true;
    r->survey.floor = VUL_DBM_INVALID;
    r->hunt.level.signal = VUL_DBM_INVALID;
    r->hunt.level.floor = VUL_DBM_INVALID;
    for(uint8_t i = 0; i < VUL_SURVEY_BINS; i++) {
        r->survey.bins[i] = VUL_DBM_INVALID;
        r->survey.hold[i] = VUL_DBM_INVALID;
    }
    return r;
}

void vul_radio_free(VulRadio* r) {
    furi_assert(r);
    vul_radio_stop(r);
    furi_mutex_free(r->mutex);
    free(r);
}

static void vul_radio_start(VulRadio* r, bool survey) {
    if(r->running) vul_radio_stop(r);
    r->survey_mode = survey;
    r->running = true;
    r->thread = furi_thread_alloc_ex("VulpesRadio", VUL_WORKER_STACK, vul_radio_thread, r);
    furi_thread_start(r->thread);
}

void vul_radio_survey_start(VulRadio* r, uint8_t band) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_band = band < VUL_BAND_COUNT ? band : 0;
    r->req_reset_peak = true;
    furi_mutex_release(r->mutex);
    vul_radio_start(r, true);
}

void vul_radio_hunt_start(VulRadio* r, uint32_t frequency, int16_t seed_floor) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_freq = frequency;
    r->req_seed_floor = seed_floor;
    r->req_atten_step = 0;
    memset(&r->hunt.trace, 0, sizeof(r->hunt.trace));
    r->hunt.trace_head = 0;
    r->hunt.updates = 0;
    r->hunt.peak_margin = 0;
    r->hunt.have_mark = false;
    furi_mutex_release(r->mutex);
    vul_radio_start(r, false);
}

void vul_radio_stop(VulRadio* r) {
    furi_assert(r);
    if(!r->running) return;
    r->running = false;
    furi_thread_join(r->thread);
    furi_thread_free(r->thread);
    r->thread = NULL;
}

void vul_radio_survey_get(VulRadio* r, VulSurveySnapshot* out) {
    furi_assert(r);
    furi_assert(out);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    *out = r->survey;
    furi_mutex_release(r->mutex);
}

void vul_radio_hunt_get(VulRadio* r, VulHuntSnapshot* out) {
    furi_assert(r);
    furi_assert(out);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    *out = r->hunt;
    furi_mutex_release(r->mutex);
}

void vul_radio_reset_peak(VulRadio* r) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_reset_peak = true;
    furi_mutex_release(r->mutex);
}

void vul_radio_reset_stats(VulRadio* r) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_reset_stats = true;
    furi_mutex_release(r->mutex);
}

void vul_radio_mark(VulRadio* r) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_mark = true;
    furi_mutex_release(r->mutex);
}

void vul_radio_set_band(VulRadio* r, uint8_t band) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_band = band < VUL_BAND_COUNT ? band : 0;
    furi_mutex_release(r->mutex);
}

void vul_radio_set_atten_auto(VulRadio* r, bool on) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_atten_auto = on;
    furi_mutex_release(r->mutex);
}

void vul_radio_set_atten_step(VulRadio* r, uint8_t step) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_atten_step = step < VUL_ATTEN_STEPS ? step : 0;
    furi_mutex_release(r->mutex);
}
