/* The hunt instrument: two pages, cycled with Left/Right.
 *
 *   HUNT   -- how close am I, and am I getting closer
 *   SIGNAL -- what kind of thing is this
 *
 * Layout constants are load-bearing; tools_gen_mockups.py mirrors them, which
 * is how collisions get found before they ship.
 */
#include "hunt_view.h"

#include <furi.h>
#include <gui/gui.h>
#include <stdio.h>
#include <string.h>

/* --- vertical bands --- */
#define HV_HEADER_BASE 9
#define HV_RULE_Y 11
#define HV_HEAT_BASE 24
#define HV_MARK_BASE 33
#define HV_TRACE_TOP 35
#define HV_TRACE_BASE 51
#define HV_TRACE_H (HV_TRACE_BASE - HV_TRACE_TOP)
#define HV_FOOTER_Y 53
#define HV_FOOTER_BASE 62

/* The trend arrow sits to the right of the heat word. */
#define HV_ARROW_CX 118
#define HV_ARROW_CY 19

struct HuntView {
    View* view;
    HuntViewCallback mark_cb;
    void* mark_ctx;
    HuntViewCallback reset_cb;
    void* reset_ctx;
    HuntViewAttenCallback atten_cb;
    void* atten_ctx;
};

typedef struct {
    VulHuntSnapshot s;
    HuntPage page;
    uint8_t anim;
} HuntModel;

/* ------------------------------------------------------------------ */

static void hv_freq_str(char* buf, size_t len, uint32_t hz) {
    uint32_t mhz = hz / 1000000UL;
    uint32_t frac = (hz % 1000000UL) / 10000UL;
    snprintf(buf, len, "%lu.%02lu", (unsigned long)mhz, (unsigned long)frac);
}

/* Draws a "\n"-separated string as consecutive lines. */
static void hv_draw_lines(Canvas* canvas, int x, int base, int step, const char* text) {
    char line[32];
    int y = base;
    while(text && *text) {
        const char* nl = strchr(text, '\n');
        size_t n = nl ? (size_t)(nl - text) : strlen(text);
        if(n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, text, n);
        line[n] = '\0';
        canvas_draw_str(canvas, x, y, line);
        y += step;
        if(!nl) break;
        text = nl + 1;
    }
}

static void hv_draw_arrow(Canvas* canvas, VulTrend trend) {
    const int cx = HV_ARROW_CX;
    const int cy = HV_ARROW_CY;
    switch(trend) {
    case VulTrendWarmer:
        canvas_draw_line(canvas, cx, cy - 5, cx - 5, cy + 4);
        canvas_draw_line(canvas, cx, cy - 5, cx + 5, cy + 4);
        canvas_draw_line(canvas, cx - 5, cy + 4, cx + 5, cy + 4);
        break;
    case VulTrendColder:
        canvas_draw_line(canvas, cx, cy + 5, cx - 5, cy - 4);
        canvas_draw_line(canvas, cx, cy + 5, cx + 5, cy - 4);
        canvas_draw_line(canvas, cx - 5, cy - 4, cx + 5, cy - 4);
        break;
    case VulTrendFlat:
        canvas_draw_line(canvas, cx - 5, cy - 2, cx + 5, cy - 2);
        canvas_draw_line(canvas, cx - 5, cy + 2, cx + 5, cy + 2);
        break;
    case VulTrendUnknown:
    default:
        /* Deliberately blank. An arrow with nothing behind it is a lie. */
        break;
    }
}

/* ------------------------------------------------------------------ *
 * Page: HUNT
 * ------------------------------------------------------------------ */

static void hv_draw_hunt(Canvas* canvas, const HuntModel* m) {
    const VulHuntSnapshot* s = &m->s;
    char buf[32];

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, HV_HEADER_BASE, "HUNT");
    hv_freq_str(buf, sizeof(buf), s->frequency);
    canvas_draw_str_aligned(canvas, 126, HV_HEADER_BASE, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, HV_RULE_Y, 127, HV_RULE_Y);

    if(!s->valid) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "NO CC1101");
        return;
    }

    /* --- the headline: how hot --- */
    VulHeat heat = vul_heat(s->level.margin);
    const char* word = s->level.valid ? vul_heat_name(heat) : "LISTENING";

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 56, HV_HEAT_BASE - 5, AlignCenter, AlignCenter, word);

    /* ON TOP gets boxed, so it reads mid-sweep without looking at the screen
     * straight on. */
    if(s->level.valid && heat == VulHeatOnTop) {
        /* Hugs the glyphs: two clear pixels under the header rule above and
         * above the mark line below, which is all the room there is. */
        int w = canvas_string_width(canvas, word);
        canvas_draw_frame(canvas, 56 - w / 2 - 3, HV_HEAT_BASE - 11, w + 6, 13);
    }

    hv_draw_arrow(canvas, s->level.valid ? s->trend.trend : VulTrendUnknown);

    /* --- the mark line --- */
    canvas_set_font(canvas, FontSecondary);
    if(s->have_mark && s->level.valid) {
        int16_t halvings = vul_halvings_x10(s->level.margin, s->mark_margin);
        uint16_t ratio = vul_range_ratio_x10(halvings);
        snprintf(
            buf,
            sizeof(buf),
            "~%u.%ux %s than mark",
            (unsigned)(ratio / 10),
            (unsigned)(ratio % 10),
            halvings >= 0 ? "closer" : "further");
    } else {
        snprintf(buf, sizeof(buf), "OK: mark this distance");
    }
    canvas_draw_str_aligned(canvas, 64, HV_MARK_BASE, AlignCenter, AlignBottom, buf);

    /* --- the trace: the shape of the sweep you just made --- */
    canvas_draw_line(canvas, 0, HV_TRACE_BASE + 1, 127, HV_TRACE_BASE + 1);
    for(int k = 0; k < VUL_TRACE_LEN; k++) {
        int idx = (s->trace_head - k + 2 * VUL_TRACE_LEN) % VUL_TRACE_LEN;
        int v = s->trace[idx];
        int x = 126 - k * 2;
        int y = HV_TRACE_BASE - (v * HV_TRACE_H) / 100;
        if(y < HV_TRACE_BASE)
            canvas_draw_line(canvas, x, HV_TRACE_BASE, x, y);
        else
            canvas_draw_dot(canvas, x, HV_TRACE_BASE);
    }

    /* Peak-hold, dashed so it cannot be mistaken for the live trace. */
    if(s->peak_margin > 0) {
        int pv = s->peak_margin >= VUL_TRACE_RANGE_DB ?
                     100 :
                     ((int)s->peak_margin * 100) / VUL_TRACE_RANGE_DB;
        int py = HV_TRACE_BASE - (pv * HV_TRACE_H) / 100;
        for(int x = 0; x < 128; x += 4) canvas_draw_dot(canvas, x, py);
    }

    /* --- footer --- */
    canvas_draw_box(canvas, 0, HV_FOOTER_Y, 128, 64 - HV_FOOTER_Y);
    canvas_set_color(canvas, ColorWhite);
    if(s->level.valid) {
        snprintf(
            buf, sizeof(buf), "%d dBm  +%d", (int)s->level.signal, (int)s->level.margin);
    } else {
        snprintf(buf, sizeof(buf), "%d dBm", (int)s->rssi);
    }
    canvas_draw_str(canvas, 3, HV_FOOTER_BASE, buf);

    /* The attenuator wins the right-hand slot when it is engaged: while it is,
     * the dBm on the left is no longer the true level and the user has to know
     * that more than they need a key hint. */
    if(s->atten_step > 0) {
        snprintf(buf, sizeof(buf), "ATT -%ukHz", (unsigned)s->atten_khz);
    } else {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)s->trend.confidence);
    }
    canvas_draw_str_aligned(canvas, 125, HV_FOOTER_BASE, AlignRight, AlignBottom, buf);
    canvas_set_color(canvas, ColorBlack);
}

/* ------------------------------------------------------------------ *
 * Page: SIGNAL
 * ------------------------------------------------------------------ */

static void hv_draw_signal(Canvas* canvas, const HuntModel* m) {
    const VulHuntSnapshot* s = &m->s;
    char buf[32];

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, HV_HEADER_BASE, "SIGNAL");
    hv_freq_str(buf, sizeof(buf), s->frequency);
    canvas_draw_str_aligned(canvas, 126, HV_HEADER_BASE, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, HV_RULE_Y, 127, HV_RULE_Y);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 21, AlignCenter, AlignCenter, vul_sig_name(s->sig.kind));

    canvas_set_font(canvas, FontSecondary);

    snprintf(buf, sizeof(buf), "Duty %u%%", (unsigned)s->sig.duty_pct);
    canvas_draw_str(canvas, 2, 34, buf);

    if(s->sig.period_ms > 0) {
        unsigned p = s->sig.period_ms;
        if(p >= 1000) {
            snprintf(buf, sizeof(buf), "every %u.%us", p / 1000, (p % 1000) / 100);
        } else {
            snprintf(buf, sizeof(buf), "every %ums", p);
        }
    } else {
        snprintf(buf, sizeof(buf), "%u bursts", (unsigned)s->sig.bursts);
    }
    canvas_draw_str_aligned(canvas, 126, 34, AlignRight, AlignBottom, buf);

    canvas_draw_line(canvas, 0, 37, 127, 37);

    /* Two lines of hint at baselines 46 and 55. There is deliberately no rule
     * above the bottom line: at this spacing one would strike through the
     * second hint line. */
    hv_draw_lines(canvas, 2, 46, 9, vul_sig_hint(s->sig.kind));

    /* Bottom line: what the radio is doing, so the classification can be read
     * against how much evidence is behind it. */
    snprintf(
        buf,
        sizeof(buf),
        "%u Hz  %lus listened",
        (unsigned)s->sample_hz,
        (unsigned long)(s->updates / VUL_TRACK_HZ));
    canvas_draw_str(canvas, 2, HV_FOOTER_BASE, buf);
}

/* ------------------------------------------------------------------ */

static void hunt_view_draw(Canvas* canvas, void* model) {
    HuntModel* m = model;
    canvas_clear(canvas);
    if(m->page == HuntPageSignal)
        hv_draw_signal(canvas, m);
    else
        hv_draw_hunt(canvas, m);
}

static bool hunt_view_input(InputEvent* event, void* context) {
    HuntView* v = context;

    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyOk:
            if(v->mark_cb) v->mark_cb(v->mark_ctx);
            return true;
        case InputKeyLeft:
        case InputKeyRight: {
            with_view_model(
                v->view,
                HuntModel * m,
                { m->page = (m->page == HuntPageHunt) ? HuntPageSignal : HuntPageHunt; },
                true);
            return true;
        }
        case InputKeyUp:
        case InputKeyDown: {
            uint8_t step = 0;
            with_view_model(v->view, HuntModel * m, { step = m->s.atten_step; }, false);
            if(event->key == InputKeyUp) {
                if(step + 1 < VUL_ATTEN_STEPS) step++;
            } else if(step > 0) {
                step--;
            }
            if(v->atten_cb) v->atten_cb(v->atten_ctx, step);
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

HuntView* hunt_view_alloc(void) {
    HuntView* v = malloc(sizeof(HuntView));
    memset(v, 0, sizeof(HuntView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, hunt_view_draw);
    view_set_input_callback(v->view, hunt_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(HuntModel));
    return v;
}

void hunt_view_free(HuntView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* hunt_view_get_view(HuntView* v) {
    furi_assert(v);
    return v->view;
}

void hunt_view_update(HuntView* v, const VulHuntSnapshot* snap) {
    furi_assert(v);
    furi_assert(snap);
    with_view_model(v->view, HuntModel * m, { m->s = *snap; }, true);
}

void hunt_view_tick(HuntView* v) {
    furi_assert(v);
    with_view_model(v->view, HuntModel * m, { m->anim++; }, true);
}

void hunt_view_set_mark_callback(HuntView* v, HuntViewCallback cb, void* context) {
    furi_assert(v);
    v->mark_cb = cb;
    v->mark_ctx = context;
}

void hunt_view_set_reset_callback(HuntView* v, HuntViewCallback cb, void* context) {
    furi_assert(v);
    v->reset_cb = cb;
    v->reset_ctx = context;
}

void hunt_view_set_atten_callback(HuntView* v, HuntViewAttenCallback cb, void* context) {
    furi_assert(v);
    v->atten_cb = cb;
    v->atten_ctx = context;
}
