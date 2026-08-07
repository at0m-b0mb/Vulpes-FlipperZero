#include "vul_store.h"
#include "vul_radio.h" /* VUL_BAND_COUNT */

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define VUL_SETTINGS_PATH APP_DATA_PATH("settings.bin")
#define VUL_SETTINGS_MAGIC 0xF0
#define VUL_SETTINGS_VERSION 1

const uint8_t vul_turn_seconds[VUL_TURN_COUNT] = {8, 12, 20};
const char* const vul_turn_labels[VUL_TURN_COUNT] = {"8 s", "12 s", "20 s"};

static void vul_store_ensure_dir(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);
}

void vul_store_settings_save(const VulpesSettings* s) {
    furi_assert(s);
    vul_store_ensure_dir();
    saved_struct_save(
        VUL_SETTINGS_PATH, s, sizeof(VulpesSettings), VUL_SETTINGS_MAGIC, VUL_SETTINGS_VERSION);
}

void vul_store_settings_load(VulpesSettings* s) {
    furi_assert(s);
    VulpesSettings loaded;
    if(!saved_struct_load(
           VUL_SETTINGS_PATH,
           &loaded,
           sizeof(VulpesSettings),
           VUL_SETTINGS_MAGIC,
           VUL_SETTINGS_VERSION)) {
        return; /* nothing valid on disk - the caller keeps its defaults */
    }
    /* Never let a file on the SD card index an array. */
    if(loaded.band_index >= VUL_BAND_COUNT) loaded.band_index = 3;
    if(loaded.turn_index >= VUL_TURN_COUNT) loaded.turn_index = 1;
    *s = loaded;
}
