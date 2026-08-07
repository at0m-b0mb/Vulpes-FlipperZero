#pragma once

#include <gui/view.h>
#include "../helpers/vul_radio.h"

typedef struct SurveyView SurveyView;

typedef void (*SurveyViewLockCallback)(void* context, uint32_t frequency, int16_t floor);
typedef void (*SurveyViewBandCallback)(void* context, uint8_t band);
typedef void (*SurveyViewResetCallback)(void* context);

SurveyView* survey_view_alloc(void);
void survey_view_free(SurveyView* v);
View* survey_view_get_view(SurveyView* v);

void survey_view_update(SurveyView* v, const VulSurveySnapshot* snap);
void survey_view_tick(SurveyView* v);
/** Reset the selection, e.g. when the scene is (re)entered. */
void survey_view_reset_selection(SurveyView* v);

void survey_view_set_lock_callback(SurveyView* v, SurveyViewLockCallback cb, void* context);
void survey_view_set_band_callback(SurveyView* v, SurveyViewBandCallback cb, void* context);
void survey_view_set_reset_callback(SurveyView* v, SurveyViewResetCallback cb, void* context);
