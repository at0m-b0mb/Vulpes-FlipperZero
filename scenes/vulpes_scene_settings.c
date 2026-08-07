#include "../vulpes_i.h"

static const char* const on_off[] = {"OFF", "ON"};
static const char* const auto_manual[] = {"MANUAL", "AUTO"};

static void vulpes_settings_band_cb(VariableItem* item) {
    VulpesApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.band_index = idx;
    variable_item_set_current_value_text(item, vul_bands[idx].label);
}

static void vulpes_settings_turn_cb(VariableItem* item) {
    VulpesApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.turn_index = idx;
    variable_item_set_current_value_text(item, vul_turn_labels[idx]);
}

static void vulpes_settings_atten_cb(VariableItem* item) {
    VulpesApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.atten_auto = idx > 0;
    variable_item_set_current_value_text(item, auto_manual[idx]);
}

static void vulpes_settings_sound_cb(VariableItem* item) {
    VulpesApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.sound = idx > 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

static void vulpes_settings_led_cb(VariableItem* item) {
    VulpesApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.led = idx > 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

void vulpes_scene_settings_on_enter(void* context) {
    VulpesApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(list, "Survey band", VUL_BAND_COUNT, vulpes_settings_band_cb, app);
    variable_item_set_current_value_index(item, app->settings.band_index);
    variable_item_set_current_value_text(item, vul_bands[app->settings.band_index].label);

    item = variable_item_list_add(list, "Turn time", VUL_TURN_COUNT, vulpes_settings_turn_cb, app);
    variable_item_set_current_value_index(item, app->settings.turn_index);
    variable_item_set_current_value_text(item, vul_turn_labels[app->settings.turn_index]);

    item = variable_item_list_add(list, "Attenuator", 2, vulpes_settings_atten_cb, app);
    variable_item_set_current_value_index(item, app->settings.atten_auto ? 1 : 0);
    variable_item_set_current_value_text(item, auto_manual[app->settings.atten_auto ? 1 : 0]);

    item = variable_item_list_add(list, "Sound", 2, vulpes_settings_sound_cb, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.sound ? 1 : 0]);

    item = variable_item_list_add(list, "LED", 2, vulpes_settings_led_cb, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.led ? 1 : 0]);

    view_dispatcher_switch_to_view(app->view_dispatcher, VulpesViewSettings);
}

bool vulpes_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void vulpes_scene_settings_on_exit(void* context) {
    VulpesApp* app = context;
    /* Persist on leaving rather than only at app exit, so a battery pull on
     * the next screen does not lose the change. */
    vul_store_settings_save(&app->settings);
    variable_item_list_reset(app->var_item_list);
}
