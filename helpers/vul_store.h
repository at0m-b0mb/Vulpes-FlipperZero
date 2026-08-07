/* Settings persistence. Small enough to be one saved_struct on the SD card. */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* How long a full turn takes in Bearing mode. Slower is better data -- the
 * rose bins by elapsed time, so an even pace is the whole technique -- but
 * standing in a corridor turning slowly is conspicuous, hence the choice. */
#define VUL_TURN_COUNT 3
extern const uint8_t vul_turn_seconds[VUL_TURN_COUNT];
extern const char* const vul_turn_labels[VUL_TURN_COUNT];

typedef struct {
    uint8_t band_index; /* survey band */
    uint8_t turn_index; /* bearing turn duration */
    bool atten_auto;
    bool sound;
    bool led;
} VulpesSettings;

void vul_store_settings_save(const VulpesSettings* s);
void vul_store_settings_load(VulpesSettings* s);
