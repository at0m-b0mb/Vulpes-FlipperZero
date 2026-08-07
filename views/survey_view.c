/* The survey screen: a max-held spectrum of the band with the candidates
 * called out on it, and one of them selected for the hunt.
 *
 * The bars are the max-hold rather than the live sweep on purpose. A live
 * trace of a band full of bursty traffic is a flickering mess that shows
 * nothing; the hold accumulates every burst the sweep ever caught, so what you
 * see is "everything that has transmitted here since you arrived".
 */
#include "survey_view.h"

#include <furi.h>
#include <gui/gui.h>
#include <stdio.h>
#include <string.h>

#include "vulpes_icons.h" /* generated from icons/ by fbt */

#define SV_HEADER_BASE 9
#define SV_RULE_Y 11
/* The spectrum starts at 16, not 14: the selected candidate's cursor cap sits
 * three pixels above it, and at 14 that cap landed on the header rule. */
#define SV_SPEC_TOP 16
#define SV_SPEC_BASE 40
#define SV_SPEC_H (SV_SPEC_BASE - SV_SPEC_TOP)
#define SV_READ_BASE 50
#define SV_FOOTER_Y 53
#define SV_FOOTER_BASE 62

struct SurveyView {
    View* view;
    SurveyViewLockCallback lock_cb;
    void* lock_ctx;
    SurveyViewBandCallback band_cb;
    void* band_ctx;
    SurveyViewResetCallback reset_cb;
    void* reset_ctx;
};

typedef struct {
    VulSurveySnapshot s;
    uint8_t sel;
    uint8_t anim;
} SurveyModel;

/* ------------------------------------------------------------------ */

static void sv_freq_str(char* buf, size_t len, uint32_t hz) {
    uint32_t mhz = hz / 1000000UL;
    uint32_t frac = (hz % 1000000UL) / 10000UL;
    snprintf(buf, len, "%lu.%02lu", (unsigned long)mhz, (unsigned long)frac);
}

static int sv_bar_height(int16_t dbm, int16_t floor) {
    if(dbm == VUL_DBM_INVALID || floor == VUL_DBM_INVALID) return 0;
    int margin = dbm - floor;
    if(margin <= 0) return 0;
    if(margin >= VUL_TRACE_RANGE_DB) return SV_SPEC_H;
    return (margin * SV_SPEC_H) / VUL_TRACE_RANGE_DB;
}

static void survey_view_draw(Canvas* canvas, void* model) {
    SurveyModel* m = model;
    const VulSurveySnapshot* s = &m->s;
    char buf[36];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    /* --- header --- */
    canvas_draw_icon(canvas, 1, 1, &I_wave_10px);
    canvas_draw_str(canvas, 14, SV_HEADER_BASE, "SURVEY");
    canvas_draw_str_aligned(
        canvas,
        126,
        SV_HEADER_BASE,
        AlignRight,
        AlignBottom,
        vul_bands[s->band < VUL_BAND_COUNT ? s->band : 0].label);
    canvas_draw_line(canvas, 0, SV_RULE_Y, 127, SV_RULE_Y);

    if(!s->valid) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "NO CC1101");
        return;
    }

    /* --- spectrum --- */
    canvas_draw_line(canvas, 0, SV_SPEC_BASE + 1, 127, SV_SPEC_BASE + 1);
    for(uint8_t bin = 0; bin < VUL_SURVEY_BINS; bin++) {
        int h = sv_bar_height(s->hold[bin], s->floor);
        int x = bin * 2;
        if(h > 0) {
            canvas_draw_box(canvas, x, SV_SPEC_BASE - h, 2, h);
        } else {
            canvas_draw_dot(canvas, x, SV_SPEC_BASE);
        }
    }

    /* --- candidate markers --- */
    for(uint8_t i = 0; i < s->cand_count; i++) {
        int x = s->cand[i].bin * 2;
        bool selected = (i == m->sel);
        if(selected) {
            /* A full-height cursor: it has to be findable against 64 bars. */
            for(int y = SV_SPEC_TOP - 1; y <= SV_SPEC_BASE; y += 3) {
                canvas_draw_dot(canvas, x, y);
                canvas_draw_dot(canvas, x + 1, y);
            }
            canvas_draw_box(canvas, x - 1, SV_SPEC_TOP - 3, 4, 3);
        } else {
            canvas_draw_dot(canvas, x, SV_SPEC_TOP - 2);
            canvas_draw_dot(canvas, x + 1, SV_SPEC_TOP - 2);
        }
    }

    /* --- selected candidate readout --- */
    if(s->cand_count > 0) {
        uint8_t i = m->sel < s->cand_count ? m->sel : 0;
        const VulCandidate* c = &s->cand[i];
        char freq[12];
        sv_freq_str(freq, sizeof(freq), vul_bin_freq(s->band, c->bin));

        /* Clamped so the compiler can bound the field widths; -Werror=format-
         * truncation will not take a width it cannot prove. */
        int dbm = c->dbm;
        if(dbm < -199) dbm = -199;
        if(dbm > 99) dbm = 99;
        int margin = c->margin;
        if(margin < 0) margin = 0;
        if(margin > 199) margin = 199;

        snprintf(buf, sizeof(buf), "%s MHz  %d dBm  +%d", freq, dbm, margin);
        canvas_draw_str(canvas, 2, SV_READ_BASE, buf);
    } else if(s->sweeps == 0) {
        /* The band's note, so someone who does not know where to look at least
         * knows what normally lives here. */
        snprintf(
            buf,
            sizeof(buf),
            "sweeping - %s",
            vul_bands[s->band < VUL_BAND_COUNT ? s->band : 0].note);
        canvas_draw_str(canvas, 2, SV_READ_BASE, buf);
    } else {
        snprintf(buf, sizeof(buf), "nothing over +%d dB yet", VUL_CANDIDATE_MIN_DB);
        canvas_draw_str(canvas, 2, SV_READ_BASE, buf);
    }

    /* --- footer --- */
    canvas_draw_box(canvas, 0, SV_FOOTER_Y, 128, 64 - SV_FOOTER_Y);
    canvas_set_color(canvas, ColorWhite);

    unsigned long sweeps = (unsigned long)(s->sweeps > 99999 ? 99999 : s->sweeps);
    if(s->floor != VUL_DBM_INVALID) {
        snprintf(
            buf,
            sizeof(buf),
            "%u found  %lu sw  %d",
            (unsigned)s->cand_count,
            sweeps,
            (int)s->floor);
    } else {
        snprintf(buf, sizeof(buf), "%lu sweeps", sweeps);
    }
    canvas_draw_str(canvas, 3, SV_FOOTER_BASE, buf);
    canvas_draw_str_aligned(
        canvas,
        125,
        SV_FOOTER_BASE,
        AlignRight,
        AlignBottom,
        s->cand_count > 0 ? "OK hunt" : "< band >");
    canvas_set_color(canvas, ColorBlack);
}

static bool survey_view_input(InputEvent* event, void* context) {
    SurveyView* v = context;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
        case InputKeyDown: {
            with_view_model(
                v->view,
                SurveyModel * m,
                {
                    uint8_t count = m->s.cand_count;
                    if(count > 0) {
                        if(event->key == InputKeyDown) {
                            m->sel = (uint8_t)((m->sel + 1) % count);
                        } else {
                            m->sel = (uint8_t)((m->sel + count - 1) % count);
                        }
                    }
                },
                true);
            return true;
        }
        case InputKeyLeft:
        case InputKeyRight: {
            uint8_t band = 0;
            with_view_model(v->view, SurveyModel * m, { band = m->s.band; }, false);
            if(event->key == InputKeyRight) {
                band = (uint8_t)((band + 1) % VUL_BAND_COUNT);
            } else {
                band = (uint8_t)((band + VUL_BAND_COUNT - 1) % VUL_BAND_COUNT);
            }
            with_view_model(v->view, SurveyModel * m, { m->sel = 0; }, false);
            if(v->band_cb) v->band_cb(v->band_ctx, band);
            return true;
        }
        case InputKeyOk: {
            uint32_t freq = 0;
            int16_t floor = VUL_DBM_INVALID;
            with_view_model(
                v->view,
                SurveyModel * m,
                {
                    if(m->s.cand_count > 0) {
                        uint8_t i = m->sel < m->s.cand_count ? m->sel : 0;
                        freq = vul_bin_freq(m->s.band, m->s.cand[i].bin);
                        floor = m->s.floor;
                    }
                },
                false);
            /* Called even with nothing selected: the scene turns freq == 0
             * into a buzz, so pressing OK at an empty band is answered rather
             * than silently ignored. */
            if(v->lock_cb) v->lock_cb(v->lock_ctx, freq, floor);
            return true;
        }
        default:
            break;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        if(v->reset_cb) v->reset_cb(v->reset_ctx);
        return true;
    }

    return false;
}

SurveyView* survey_view_alloc(void) {
    SurveyView* v = malloc(sizeof(SurveyView));
    memset(v, 0, sizeof(SurveyView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, survey_view_draw);
    view_set_input_callback(v->view, survey_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SurveyModel));
    return v;
}

void survey_view_free(SurveyView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* survey_view_get_view(SurveyView* v) {
    furi_assert(v);
    return v->view;
}

void survey_view_update(SurveyView* v, const VulSurveySnapshot* snap) {
    furi_assert(v);
    furi_assert(snap);
    with_view_model(
        v->view,
        SurveyModel * m,
        {
            m->s = *snap;
            if(m->s.cand_count > 0 && m->sel >= m->s.cand_count) {
                m->sel = (uint8_t)(m->s.cand_count - 1);
            }
        },
        true);
}

void survey_view_tick(SurveyView* v) {
    furi_assert(v);
    with_view_model(v->view, SurveyModel * m, { m->anim++; }, true);
}

void survey_view_reset_selection(SurveyView* v) {
    furi_assert(v);
    with_view_model(v->view, SurveyModel * m, { m->sel = 0; }, true);
}

void survey_view_set_lock_callback(SurveyView* v, SurveyViewLockCallback cb, void* context) {
    furi_assert(v);
    v->lock_cb = cb;
    v->lock_ctx = context;
}

void survey_view_set_band_callback(SurveyView* v, SurveyViewBandCallback cb, void* context) {
    furi_assert(v);
    v->band_cb = cb;
    v->band_ctx = context;
}

void survey_view_set_reset_callback(SurveyView* v, SurveyViewResetCallback cb, void* context) {
    furi_assert(v);
    v->reset_cb = cb;
    v->reset_ctx = context;
}
