#pragma once

#include <gui/view.h>

typedef struct SplashView SplashView;

typedef void (*SplashViewSkipCallback)(void* context);

SplashView* splash_view_alloc(void);
void splash_view_free(SplashView* v);
View* splash_view_get_view(SplashView* v);

void splash_view_set_progress(SplashView* v, uint8_t percent);
void splash_view_tick(SplashView* v);
void splash_view_set_skip_callback(SplashView* v, SplashViewSkipCallback cb, void* context);
