#pragma once

#include <gui/view.h>
#include "../helpers/vul_df.h"

typedef struct BearingView BearingView;

typedef enum {
    BearingStateIdle,
    BearingStateTurning,
    BearingStateDone,
} BearingState;

typedef struct {
    VulRose rose;
    VulRoseResult result;
    BearingState state;
    uint8_t progress; /* 0..100 through the turn */
    uint8_t sector; /* sector being filled right now */
    int16_t margin; /* live margin, for the operator's confidence */
    uint32_t frequency;
    bool valid; /* radio came up */
} BearingData;

typedef void (*BearingViewCallback)(void* context);

BearingView* bearing_view_alloc(void);
void bearing_view_free(BearingView* v);
View* bearing_view_get_view(BearingView* v);

void bearing_view_update(BearingView* v, const BearingData* data);
void bearing_view_tick(BearingView* v);

/** OK: start a turn, or start another one. */
void bearing_view_set_start_callback(BearingView* v, BearingViewCallback cb, void* context);
