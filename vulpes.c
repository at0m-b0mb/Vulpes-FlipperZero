#include "vulpes_i.h"
#include <string.h>

/* ---------------- feedback ----------------
 *
 * The hunt ping is the important one. An operator sweeping a room is looking
 * at the skirting board, not at the screen, so the pitch and the rate of the
 * click have to carry the reading on their own.
 *
 * Every sequence below is deliberately the same length, so they can live in
 * one array without the pointer-type mismatch that different-length
 * NotificationSequences produce.
 */
static const NotificationSequence seq_ping_cold = {
    &message_note_c4,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_ping_cool = {
    &message_note_g4,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_ping_warm = {
    &message_note_c5,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_ping_hot = {
    &message_note_e5,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_ping_burning = {
    &message_note_g5,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_ping_ontop = {
    &message_note_c6,
    &message_delay_10,
    &message_sound_off,
    NULL,
};

static const NotificationSequence* const seq_ping[VulHeatCount] = {
    &seq_ping_cold,
    &seq_ping_cool,
    &seq_ping_warm,
    &seq_ping_hot,
    &seq_ping_burning,
    &seq_ping_ontop,
};

/* Milliseconds between pings, per heat. A geiger counter, not a metronome. */
static const uint16_t ping_interval_ms[VulHeatCount] = {1200, 700, 450, 260, 140, 70};

static const NotificationSequence seq_snd_lock = {
    &message_note_e5,
    &message_delay_50,
    &message_note_a5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_snd_found = {
    &message_note_c5,
    &message_delay_50,
    &message_note_e5,
    &message_delay_50,
    &message_note_g5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_snd_reject = {
    &message_note_c4,
    &message_delay_100,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_led_lock = {
    &message_blue_255,
    &message_delay_50,
    &message_blue_0,
    NULL,
};
static const NotificationSequence seq_led_found = {
    &message_green_255,
    &message_delay_250,
    &message_green_0,
    NULL,
};

uint32_t vulpes_ping_interval(VulHeat heat) {
    return ping_interval_ms[heat < VulHeatCount ? heat : 0];
}

void vulpes_notify_ping(VulpesApp* app, VulHeat heat) {
    furi_assert(app);
    if(!app->settings.sound) return;
    notification_message(app->notifications, seq_ping[heat < VulHeatCount ? heat : 0]);
}

void vulpes_notify_lock(VulpesApp* app) {
    furi_assert(app);
    if(app->settings.led) notification_message(app->notifications, &seq_led_lock);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_lock);
}

void vulpes_notify_found(VulpesApp* app) {
    furi_assert(app);
    if(app->settings.led) notification_message(app->notifications, &seq_led_found);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_found);
}

void vulpes_notify_reject(VulpesApp* app) {
    furi_assert(app);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_reject);
}

/* ---------------- view dispatcher plumbing ---------------- */

static bool vulpes_custom_event_callback(void* context, uint32_t event) {
    VulpesApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool vulpes_back_event_callback(void* context) {
    VulpesApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void vulpes_tick_event_callback(void* context) {
    VulpesApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ---------------- lifecycle ---------------- */

static VulpesApp* vulpes_app_alloc(void) {
    VulpesApp* app = malloc(sizeof(VulpesApp));
    memset(app, 0, sizeof(VulpesApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&vulpes_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, vulpes_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, vulpes_back_event_callback);
    /* 100 ms: fast enough for the hunt to feel live, slow enough that the GUI
     * is not fighting the radio worker for the bus. */
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, vulpes_tick_event_callback, 100);

    /* Defaults first, then whatever was saved last run. */
    app->settings.band_index = 3; /* 433 ISM */
    app->settings.turn_index = 1; /* 12 s */
    app->settings.atten_auto = true;
    app->settings.sound = true;
    app->settings.led = true;
    vul_store_settings_load(&app->settings);

    app->locked_freq = VULPES_DEFAULT_FREQ;
    app->locked_floor = VUL_DBM_INVALID;
    app->radio = vul_radio_alloc();
    vul_rose_init(&app->rose);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, VulpesViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, VulpesViewSettings, variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, VulpesViewAbout, widget_get_view(app->widget));

    app->splash_view = splash_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, VulpesViewSplash, splash_view_get_view(app->splash_view));

    app->survey_view = survey_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, VulpesViewSurvey, survey_view_get_view(app->survey_view));

    app->hunt_view = hunt_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, VulpesViewHunt, hunt_view_get_view(app->hunt_view));

    app->bearing_view = bearing_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, VulpesViewBearing, bearing_view_get_view(app->bearing_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void vulpes_app_free(VulpesApp* app) {
    furi_assert(app);

    vul_radio_stop(app->radio);
    vul_store_settings_save(&app->settings);

    view_dispatcher_remove_view(app->view_dispatcher, VulpesViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, VulpesViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, VulpesViewAbout);
    view_dispatcher_remove_view(app->view_dispatcher, VulpesViewSplash);
    view_dispatcher_remove_view(app->view_dispatcher, VulpesViewSurvey);
    view_dispatcher_remove_view(app->view_dispatcher, VulpesViewHunt);
    view_dispatcher_remove_view(app->view_dispatcher, VulpesViewBearing);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    splash_view_free(app->splash_view);
    survey_view_free(app->survey_view);
    hunt_view_free(app->hunt_view);
    bearing_view_free(app->bearing_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    vul_radio_free(app->radio);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t vulpes_app(void* p) {
    UNUSED(p);
    VulpesApp* app = vulpes_app_alloc();
    scene_manager_next_scene(app->scene_manager, VulpesSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    vulpes_app_free(app);
    return 0;
}
