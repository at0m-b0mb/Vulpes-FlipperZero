#include "../vulpes_i.h"

static void vulpes_bearing_start_cb(void* context) {
    VulpesApp* app = context;
    vul_rose_init(&app->rose);
    app->turn_done = false;
    app->turn_started = furi_get_tick();
    if(app->turn_started == 0) app->turn_started = 1; /* 0 means idle */
    vulpes_notify_lock(app);
}

void vulpes_scene_bearing_on_enter(void* context) {
    VulpesApp* app = context;

    bearing_view_set_start_callback(app->bearing_view, vulpes_bearing_start_cb, app);

    vul_rose_init(&app->rose);
    app->turn_started = 0;
    app->turn_done = false;

    uint32_t freq = app->have_lock ? app->locked_freq : VULPES_DEFAULT_FREQ;
    int16_t floor = app->have_lock ? app->locked_floor : VUL_DBM_INVALID;

    /* Bearing rides the same fixed-frequency hunt loop; only the presentation
     * differs. */
    vul_radio_set_atten_auto(app->radio, app->settings.atten_auto);
    vul_radio_hunt_start(app->radio, freq, floor);

    view_dispatcher_switch_to_view(app->view_dispatcher, VulpesViewBearing);
}

bool vulpes_scene_bearing_on_event(void* context, SceneManagerEvent event) {
    VulpesApp* app = context;

    if(event.type != SceneManagerEventTypeTick) return false;

    vul_radio_hunt_get(app->radio, &app->hunt);

    BearingData d;
    memset(&d, 0, sizeof(d));
    d.frequency = app->hunt.frequency;
    d.valid = app->hunt.valid;
    d.margin = app->hunt.level.valid ? app->hunt.level.margin : 0;

    if(app->turn_started != 0 && !app->turn_done) {
        uint32_t total_ms =
            (uint32_t)vul_turn_seconds[app->settings.turn_index] * 1000u;
        uint32_t elapsed = furi_get_tick() - app->turn_started;

        if(elapsed >= total_ms) {
            app->turn_done = true;
            app->turn_started = 0;
            vulpes_notify_found(app);
        } else {
            /* Sector from elapsed time. There is no magnetometer, so an even
             * pace is the technique -- the app says so on the idle screen. */
            uint8_t sector = (uint8_t)((elapsed * VUL_ROSE_SECTORS) / total_ms);
            if(sector >= VUL_ROSE_SECTORS) sector = VUL_ROSE_SECTORS - 1;
            if(app->hunt.level.valid) {
                vul_rose_add(&app->rose, sector, app->hunt.level.margin);
            }
            d.state = BearingStateTurning;
            d.progress = (uint8_t)((elapsed * 100u) / total_ms);
            d.sector = sector;
        }
    }

    if(app->turn_done) {
        d.state = BearingStateDone;
        d.progress = 100;
    } else if(app->turn_started == 0) {
        d.state = BearingStateIdle;
    }

    d.rose = app->rose;
    vul_rose_result(&app->rose, &d.result);

    bearing_view_update(app->bearing_view, &d);
    bearing_view_tick(app->bearing_view);
    return true;
}

void vulpes_scene_bearing_on_exit(void* context) {
    VulpesApp* app = context;
    vul_radio_stop(app->radio);
}
