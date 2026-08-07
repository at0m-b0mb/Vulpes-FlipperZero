#include "../vulpes_i.h"

static void vulpes_survey_lock_cb(void* context, uint32_t frequency, int16_t floor) {
    VulpesApp* app = context;

    if(frequency == 0) {
        /* Nothing found yet. Hunting an arbitrary frequency would look like it
         * was working while measuring nothing, so decline audibly instead. */
        vulpes_notify_reject(app);
        return;
    }

    app->locked_freq = frequency;
    app->locked_floor = floor;
    app->have_lock = true;
    vulpes_notify_lock(app);
    /* Navigation has to happen on the dispatch, not inside an input callback. */
    view_dispatcher_send_custom_event(app->view_dispatcher, VulpesCustomEventLock);
}

static void vulpes_survey_band_cb(void* context, uint8_t band) {
    VulpesApp* app = context;
    app->settings.band_index = band;
    vul_radio_set_band(app->radio, band);
}

static void vulpes_survey_reset_cb(void* context) {
    VulpesApp* app = context;
    vul_radio_reset_peak(app->radio);
}

void vulpes_scene_survey_on_enter(void* context) {
    VulpesApp* app = context;

    survey_view_reset_selection(app->survey_view);
    survey_view_set_lock_callback(app->survey_view, vulpes_survey_lock_cb, app);
    survey_view_set_band_callback(app->survey_view, vulpes_survey_band_cb, app);
    survey_view_set_reset_callback(app->survey_view, vulpes_survey_reset_cb, app);

    vul_radio_survey_start(app->radio, app->settings.band_index);
    view_dispatcher_switch_to_view(app->view_dispatcher, VulpesViewSurvey);
}

bool vulpes_scene_survey_on_event(void* context, SceneManagerEvent event) {
    VulpesApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        vul_radio_survey_get(app->radio, &app->survey);
        survey_view_update(app->survey_view, &app->survey);
        survey_view_tick(app->survey_view);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom && event.event == VulpesCustomEventLock) {
        scene_manager_next_scene(app->scene_manager, VulpesSceneHunt);
        return true;
    }

    return false;
}

void vulpes_scene_survey_on_exit(void* context) {
    VulpesApp* app = context;
    /* Pushing the hunt scene runs this too, which is exactly right: the hunt
     * needs the radio to itself and restarts it in the other mode. */
    vul_radio_stop(app->radio);
    vul_store_settings_save(&app->settings);
}
