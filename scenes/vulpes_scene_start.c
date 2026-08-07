#include "../vulpes_i.h"

/* The intro runs ~1.7 s at the 100 ms tick, then hands off to the menu; any
 * key skips it. It lives inside the root scene rather than on the scene stack,
 * so coming back to the menu from a hunt never replays it, and Back from the
 * menu still exits the app cleanly. */
#define VUL_SPLASH_TICKS 17

typedef enum {
    StartIndexSurvey,
    StartIndexHunt,
    StartIndexBearing,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void vulpes_scene_start_submenu_cb(void* context, uint32_t index) {
    VulpesApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void vulpes_scene_start_show_menu(VulpesApp* app) {
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Vulpes");
    submenu_add_item(
        submenu, "Survey - what is out there", StartIndexSurvey, vulpes_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "Hunt - hot and cold", StartIndexHunt, vulpes_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "Bearing - which way", StartIndexBearing, vulpes_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Settings", StartIndexSettings, vulpes_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "About", StartIndexAbout, vulpes_scene_start_submenu_cb, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, VulpesSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, VulpesViewSubmenu);
}

static void vulpes_scene_start_skip_splash(void* context) {
    VulpesApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, VulpesCustomEventSkipSplash);
}

void vulpes_scene_start_on_enter(void* context) {
    VulpesApp* app = context;

    if(!app->splash_done) {
        app->splash_ticks = 0;
        splash_view_set_progress(app->splash_view, 0);
        splash_view_set_skip_callback(app->splash_view, vulpes_scene_start_skip_splash, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, VulpesViewSplash);
    } else {
        vulpes_scene_start_show_menu(app);
    }
}

bool vulpes_scene_start_on_event(void* context, SceneManagerEvent event) {
    VulpesApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(!app->splash_done) {
            app->splash_ticks++;
            uint8_t progress = (uint8_t)((app->splash_ticks * 100u) / VUL_SPLASH_TICKS);
            splash_view_set_progress(app->splash_view, progress);
            splash_view_tick(app->splash_view);
            if(app->splash_ticks >= VUL_SPLASH_TICKS) {
                app->splash_done = true;
                vulpes_scene_start_show_menu(app);
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(!app->splash_done && event.event == VulpesCustomEventSkipSplash) {
            app->splash_done = true;
            vulpes_scene_start_show_menu(app);
            return true;
        }

        scene_manager_set_scene_state(app->scene_manager, VulpesSceneStart, event.event);
        switch(event.event) {
        case StartIndexSurvey:
            scene_manager_next_scene(app->scene_manager, VulpesSceneSurvey);
            consumed = true;
            break;
        case StartIndexHunt:
            scene_manager_next_scene(app->scene_manager, VulpesSceneHunt);
            consumed = true;
            break;
        case StartIndexBearing:
            scene_manager_next_scene(app->scene_manager, VulpesSceneBearing);
            consumed = true;
            break;
        case StartIndexSettings:
            scene_manager_next_scene(app->scene_manager, VulpesSceneSettings);
            consumed = true;
            break;
        case StartIndexAbout:
            scene_manager_next_scene(app->scene_manager, VulpesSceneAbout);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void vulpes_scene_start_on_exit(void* context) {
    VulpesApp* app = context;
    submenu_reset(app->submenu);
}
