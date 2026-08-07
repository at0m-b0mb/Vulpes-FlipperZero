#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "vulpes_icons.h" /* generated from icons/ by fbt */

#include "helpers/vul_df.h"
#include "helpers/vul_radio.h"
#include "helpers/vul_store.h"
#include "views/hunt_view.h"
#include "views/survey_view.h"
#include "views/bearing_view.h"
#include "views/splash_view.h"
#include "scenes/vulpes_scene.h"

#define VULPES_VERSION "1.0"

/* Fallback when the user opens Hunt without surveying first. */
#define VULPES_DEFAULT_FREQ 433920000

typedef enum {
    VulpesViewSplash,
    VulpesViewSubmenu,
    VulpesViewSurvey,
    VulpesViewHunt,
    VulpesViewBearing,
    VulpesViewSettings,
    VulpesViewAbout,
} VulpesViewId;

typedef enum {
    /* Above any submenu index, so the splash-skip event cannot be mistaken
     * for a menu selection. */
    VulpesCustomEventSkipSplash = 100,
    VulpesCustomEventLock, /* a candidate was chosen in Survey */
} VulpesCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;

    SplashView* splash_view;
    SurveyView* survey_view;
    HuntView* hunt_view;
    BearingView* bearing_view;

    VulRadio* radio;
    VulpesSettings settings;

    /* Snapshots live here rather than on a scene's stack: together they are
     * about half a kilobyte, and the GUI thread's stack is not the place. */
    VulSurveySnapshot survey;
    VulHuntSnapshot hunt;

    /* What Hunt and Bearing are working on. Set by Survey, or defaulted. */
    uint32_t locked_freq;
    int16_t locked_floor; /* band floor from the survey, or VUL_DBM_INVALID */
    bool have_lock;

    /* Bearing run state, owned by the scene but kept here so it survives the
     * view being rebuilt. */
    VulRose rose;
    uint32_t turn_started; /* tick the turn began, 0 when idle */
    bool turn_done;

    uint32_t last_ping_tick; /* paces the hunt's audio feedback */
    bool splash_done;
    uint8_t splash_ticks;
} VulpesApp;

/* Feedback, all gated by settings (defined in vulpes.c). */
void vulpes_notify_ping(VulpesApp* app, VulHeat heat);
void vulpes_notify_lock(VulpesApp* app);
void vulpes_notify_found(VulpesApp* app);
void vulpes_notify_reject(VulpesApp* app);

/** Milliseconds between hunt pings for a given heat: faster as it warms. */
uint32_t vulpes_ping_interval(VulHeat heat);
