#pragma once

#include <gui/view.h>
#include "../helpers/vul_radio.h"

typedef struct HuntView HuntView;

typedef enum {
    HuntPageHunt, /* the hot-and-cold instrument */
    HuntPageSignal, /* what kind of thing is transmitting */
    HuntPageCount,
} HuntPage;

typedef void (*HuntViewCallback)(void* context);
/** Step is 0..VUL_ATTEN_STEPS-1; the view never applies it itself. */
typedef void (*HuntViewAttenCallback)(void* context, uint8_t step);

HuntView* hunt_view_alloc(void);
void hunt_view_free(HuntView* v);
View* hunt_view_get_view(HuntView* v);

void hunt_view_update(HuntView* v, const VulHuntSnapshot* snap);
void hunt_view_tick(HuntView* v);

/** OK: drop or clear the distance mark. */
void hunt_view_set_mark_callback(HuntView* v, HuntViewCallback cb, void* context);
/** Long OK: clear peak-hold and burst statistics. */
void hunt_view_set_reset_callback(HuntView* v, HuntViewCallback cb, void* context);
/** Up/Down: drive the offset attenuator by hand. */
void hunt_view_set_atten_callback(HuntView* v, HuntViewAttenCallback cb, void* context);
