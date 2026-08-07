/* Launch intro: a sweep line going round a ring with a signal blooming out of
 * one bearing, then the name. Any key skips it, and it only plays once per
 * launch. */
#include "splash_view.h"

#include <furi.h>
#include <gui/gui.h>
#include <string.h>

#define SP_CX 64
#define SP_CY 24
#define SP_R 19

#define SP_STEPS 16

/* Unit circle, x1000, starting straight up and running clockwise. */
static const int16_t sp_dx[SP_STEPS] =
    {0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383};
static const int16_t sp_dy[SP_STEPS] =
    {-1000, -924, -707, -383, 0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924};

/* Where the "transmitter" sits, in sweep steps. */
#define SP_TARGET 5

struct SplashView {
    View* view;
    SplashViewSkipCallback skip_cb;
    void* skip_ctx;
};

typedef struct {
    uint8_t progress;
    uint8_t anim;
} SplashModel;

static void splash_view_draw(Canvas* canvas, void* model) {
    SplashModel* m = model;

    canvas_clear(canvas);

    /* The ring, with a tick at each of the twelve bearings. */
    canvas_draw_circle(canvas, SP_CX, SP_CY, SP_R);

    /* The sweep line. */
    uint8_t k = (uint8_t)(m->anim % SP_STEPS);
    int x = SP_CX + (sp_dx[k] * SP_R) / 1000;
    int y = SP_CY + (sp_dy[k] * SP_R) / 1000;
    canvas_draw_line(canvas, SP_CX, SP_CY, x, y);
    canvas_draw_disc(canvas, SP_CX, SP_CY, 1);

    /* A trailing echo, so the sweep reads as motion rather than a stick. */
    uint8_t k1 = (uint8_t)((k + SP_STEPS - 1) % SP_STEPS);
    int x1 = SP_CX + (sp_dx[k1] * (SP_R - 4)) / 1000;
    int y1 = SP_CY + (sp_dy[k1] * (SP_R - 4)) / 1000;
    canvas_draw_line(canvas, SP_CX, SP_CY, x1, y1);

    /* The contact: it blooms when the sweep passes over it. */
    int tx = SP_CX + (sp_dx[SP_TARGET] * (SP_R - 6)) / 1000;
    int ty = SP_CY + (sp_dy[SP_TARGET] * (SP_R - 6)) / 1000;
    int age = (int)((k + SP_STEPS - SP_TARGET) % SP_STEPS);
    if(age < 6) {
        canvas_draw_disc(canvas, tx, ty, 2);
        canvas_draw_circle(canvas, tx, ty, 3 + age);
    } else {
        canvas_draw_dot(canvas, tx, ty);
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignBottom, "VULPES");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignBottom, "fox hunt for hidden RF");

    /* Progress hairline along the very bottom. */
    uint8_t p = m->progress > 100 ? 100 : m->progress;
    canvas_draw_line(canvas, 0, 63, (128 * p) / 100, 63);
}

static bool splash_view_input(InputEvent* event, void* context) {
    SplashView* v = context;
    if(event->type == InputTypeShort || event->type == InputTypeLong) {
        if(v->skip_cb) v->skip_cb(v->skip_ctx);
        return true;
    }
    return false;
}

SplashView* splash_view_alloc(void) {
    SplashView* v = malloc(sizeof(SplashView));
    memset(v, 0, sizeof(SplashView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, splash_view_draw);
    view_set_input_callback(v->view, splash_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SplashModel));
    return v;
}

void splash_view_free(SplashView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* splash_view_get_view(SplashView* v) {
    furi_assert(v);
    return v->view;
}

void splash_view_set_progress(SplashView* v, uint8_t percent) {
    furi_assert(v);
    with_view_model(v->view, SplashModel * m, { m->progress = percent; }, true);
}

void splash_view_tick(SplashView* v) {
    furi_assert(v);
    with_view_model(v->view, SplashModel * m, { m->anim++; }, true);
}

void splash_view_set_skip_callback(SplashView* v, SplashViewSkipCallback cb, void* context) {
    furi_assert(v);
    v->skip_cb = cb;
    v->skip_ctx = context;
}
