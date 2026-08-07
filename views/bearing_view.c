/* The bearing rose.
 *
 * A whip antenna is omnidirectional, so no single reading carries a direction.
 * The technique that does work with one is to make your own body the shield:
 * hold the Flipper against your chest and turn slowly through a full circle.
 * Your torso attenuates whatever is behind it, so the peak points outward at
 * the transmitter. There is no magnetometer, so sectors come from elapsed
 * time -- turn at a steady rate and sector k lies k*30 degrees from where you
 * started.
 *
 * The spokes are auto-scaled between the weakest and strongest sector. The
 * shape is the answer; the absolute level is on the hunt screen.
 */
#include "bearing_view.h"

#include <furi.h>
#include <gui/gui.h>
#include <stdio.h>
#include <string.h>

#include "vulpes_icons.h" /* generated from icons/ by fbt */

#define BV_HEADER_BASE 9
#define BV_RULE_Y 11

#define BV_CX 34
#define BV_CY 38
#define BV_R 23
#define BV_SPOKE_MIN 5
#define BV_SPOKE_MAX 21

#define BV_PANEL_X 64

/* Unit vectors for the 12 sectors, x1000. Sector 0 points up the screen, and
 * they run clockwise, matching the direction a right-handed person turns. */
static const int16_t bv_dx[VUL_ROSE_SECTORS] =
    {0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500};
static const int16_t bv_dy[VUL_ROSE_SECTORS] =
    {-1000, -866, -500, 0, 500, 866, 1000, 866, 500, 0, -500, -866};

struct BearingView {
    View* view;
    BearingViewCallback start_cb;
    void* start_ctx;
};

typedef struct {
    BearingData d;
    uint8_t anim;
} BearingModel;

/* ------------------------------------------------------------------ */

static void bv_point(uint8_t sector, int len, int* x, int* y) {
    *x = BV_CX + (bv_dx[sector] * len) / 1000;
    *y = BV_CY + (bv_dy[sector] * len) / 1000;
}

static void bv_draw_rose(Canvas* canvas, const BearingData* d) {
    canvas_draw_circle(canvas, BV_CX, BV_CY, BV_R);

    /* Auto-scale across whatever the turn actually found. */
    int16_t lo = 0, hi = 0;
    bool any = false;
    for(uint8_t i = 0; i < VUL_ROSE_SECTORS; i++) {
        int16_t v = d->rose.best[i];
        if(v == VUL_DBM_INVALID) continue;
        if(!any) {
            lo = hi = v;
            any = true;
        } else {
            if(v < lo) lo = v;
            if(v > hi) hi = v;
        }
    }
    int span = any ? (hi - lo) : 0;

    for(uint8_t i = 0; i < VUL_ROSE_SECTORS; i++) {
        int x, y;
        int16_t v = d->rose.best[i];

        if(v == VUL_DBM_INVALID) {
            /* Unwalked: a tick on the rim, so the gap in the turn is visible. */
            bv_point(i, BV_R, &x, &y);
            canvas_draw_dot(canvas, x, y);
            continue;
        }

        int len = BV_SPOKE_MIN;
        if(span > 0) len += ((v - lo) * (BV_SPOKE_MAX - BV_SPOKE_MIN)) / span;
        else len = (BV_SPOKE_MIN + BV_SPOKE_MAX) / 2;

        bv_point(i, len, &x, &y);
        canvas_draw_line(canvas, BV_CX, BV_CY, x, y);

        bool is_best = (d->state == BearingStateDone) && d->result.conclusive &&
                       (i == d->result.sector);
        if(is_best) {
            canvas_draw_disc(canvas, x, y, 3);
        } else if(d->state == BearingStateTurning && i == d->sector) {
            canvas_draw_circle(canvas, x, y, 2);
        }
    }

    canvas_draw_disc(canvas, BV_CX, BV_CY, 1);

    /* "Start" mark at the top, so the reported bearing has a reference. */
    canvas_draw_line(canvas, BV_CX, BV_CY - BV_R - 3, BV_CX, BV_CY - BV_R - 1);
}

static void bv_draw_panel(Canvas* canvas, const BearingData* d) {
    char buf[24];
    const int x = BV_PANEL_X;

    switch(d->state) {
    case BearingStateIdle:
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, x, 22, "Hold it flat");
        canvas_draw_str(canvas, x, 31, "to your chest.");
        canvas_draw_str(canvas, x, 43, "Turn a slow,");
        canvas_draw_str(canvas, x, 52, "even circle.");
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, x, 62, "OK: start");
        break;

    case BearingStateTurning: {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, x, 21, "TURNING");
        canvas_set_font(canvas, FontPrimary);
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)(d->progress > 100 ? 100 : d->progress));
        canvas_draw_str(canvas, x, 36, buf);
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "sector %u/%u", (unsigned)(d->sector + 1), VUL_ROSE_SECTORS);
        canvas_draw_str(canvas, x, 47, buf);
        snprintf(buf, sizeof(buf), "+%d dB", (int)d->margin);
        canvas_draw_str(canvas, x, 58, buf);
        break;
    }

    case BearingStateDone:
    default:
        if(d->result.conclusive) {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, x, 21, "LOUDEST AT");
            canvas_set_font(canvas, FontPrimary);
            snprintf(buf, sizeof(buf), "%u", (unsigned)d->result.heading_deg);
            canvas_draw_str(canvas, x, 37, buf);
            /* A degree ring, drawn rather than typed: the font has no glyph. */
            int w = canvas_string_width(canvas, buf);
            canvas_draw_circle(canvas, x + w + 4, 29, 2);
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, x, 48, "from the start");
            snprintf(buf, sizeof(buf), "peak %d dB", (int)d->result.contrast);
            canvas_draw_str(canvas, x, 58, buf);
        } else {
            /* The honest answer far more often than people expect indoors. */
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, x, 22, "TOO FLAT");
            canvas_set_font(canvas, FontSecondary);
            if(d->result.visited < VUL_ROSE_MIN_VISITED) {
                canvas_draw_str(canvas, x, 34, "Turn the whole");
                canvas_draw_str(canvas, x, 43, "circle.");
            } else {
                canvas_draw_str(canvas, x, 34, "No direction");
                canvas_draw_str(canvas, x, 43, "to be had here.");
            }
            snprintf(buf, sizeof(buf), "peak %d dB", (int)d->result.contrast);
            canvas_draw_str(canvas, x, 55, buf);
            canvas_draw_str(canvas, x, 63, "OK: again");
        }
        break;
    }
}

static void bearing_view_draw(Canvas* canvas, void* model) {
    BearingModel* m = model;
    const BearingData* d = &m->d;
    char buf[24];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_icon(canvas, 1, 1, &I_bearing_10px);
    canvas_draw_str(canvas, 14, BV_HEADER_BASE, "BEARING");

    uint32_t mhz = d->frequency / 1000000UL;
    uint32_t frac = (d->frequency % 1000000UL) / 10000UL;
    snprintf(buf, sizeof(buf), "%lu.%02lu", (unsigned long)mhz, (unsigned long)frac);
    canvas_draw_str_aligned(canvas, 126, BV_HEADER_BASE, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, BV_RULE_Y, 127, BV_RULE_Y);

    if(!d->valid) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, "NO CC1101");
        return;
    }

    bv_draw_rose(canvas, d);
    bv_draw_panel(canvas, d);
}

static bool bearing_view_input(InputEvent* event, void* context) {
    BearingView* v = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(v->start_cb) v->start_cb(v->start_ctx);
        return true;
    }
    return false;
}

BearingView* bearing_view_alloc(void) {
    BearingView* v = malloc(sizeof(BearingView));
    memset(v, 0, sizeof(BearingView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, bearing_view_draw);
    view_set_input_callback(v->view, bearing_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(BearingModel));
    return v;
}

void bearing_view_free(BearingView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* bearing_view_get_view(BearingView* v) {
    furi_assert(v);
    return v->view;
}

void bearing_view_update(BearingView* v, const BearingData* data) {
    furi_assert(v);
    furi_assert(data);
    with_view_model(v->view, BearingModel * m, { m->d = *data; }, true);
}

void bearing_view_tick(BearingView* v) {
    furi_assert(v);
    with_view_model(v->view, BearingModel * m, { m->anim++; }, true);
}

void bearing_view_set_start_callback(BearingView* v, BearingViewCallback cb, void* context) {
    furi_assert(v);
    v->start_cb = cb;
    v->start_ctx = context;
}
