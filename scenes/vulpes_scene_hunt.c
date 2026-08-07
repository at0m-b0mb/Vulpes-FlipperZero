#include "../vulpes_i.h"

static void vulpes_hunt_mark_cb(void* context) {
    VulpesApp* app = context;
    vul_radio_mark(app->radio);
    vulpes_notify_lock(app);
}

static void vulpes_hunt_reset_cb(void* context) {
    VulpesApp* app = context;
    vul_radio_reset_peak(app->radio);
    vul_radio_reset_stats(app->radio);
    vulpes_notify_lock(app);
}

static void vulpes_hunt_atten_cb(void* context, uint8_t step) {
    VulpesApp* app = context;
    /* Touching the step by hand means the operator has taken over. */
    app->settings.atten_auto = false;
    vul_radio_set_atten_auto(app->radio, false);
    vul_radio_set_atten_step(app->radio, step);
}

void vulpes_scene_hunt_on_enter(void* context) {
    VulpesApp* app = context;

    hunt_view_set_mark_callback(app->hunt_view, vulpes_hunt_mark_cb, app);
    hunt_view_set_reset_callback(app->hunt_view, vulpes_hunt_reset_cb, app);
    hunt_view_set_atten_callback(app->hunt_view, vulpes_hunt_atten_cb, app);

    uint32_t freq = app->have_lock ? app->locked_freq : VULPES_DEFAULT_FREQ;
    int16_t floor = app->have_lock ? app->locked_floor : VUL_DBM_INVALID;

    vul_radio_set_atten_auto(app->radio, app->settings.atten_auto);
    vul_radio_hunt_start(app->radio, freq, floor);

    app->last_ping_tick = 0;
    view_dispatcher_switch_to_view(app->view_dispatcher, VulpesViewHunt);
}

bool vulpes_scene_hunt_on_event(void* context, SceneManagerEvent event) {
    VulpesApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        vul_radio_hunt_get(app->radio, &app->hunt);
        hunt_view_update(app->hunt_view, &app->hunt);
        hunt_view_tick(app->hunt_view);

        /* Audio is the point of a hunt, not decoration: the operator is
         * looking at skirting boards and smoke alarms, not at the screen. Both
         * the pitch and the rate climb as it warms. */
        if(app->settings.sound && app->hunt.level.valid) {
            VulHeat heat = vul_heat(app->hunt.level.margin);
            uint32_t now = furi_get_tick();
            uint32_t due = vulpes_ping_interval(heat);
            if(due && (now - app->last_ping_tick) >= due) {
                app->last_ping_tick = now;
                vulpes_notify_ping(app, heat);
            }
        }
        return true;
    }

    return false;
}

void vulpes_scene_hunt_on_exit(void* context) {
    VulpesApp* app = context;
    vul_radio_stop(app->radio);
    vul_store_settings_save(&app->settings);
}
