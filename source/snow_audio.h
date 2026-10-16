#pragma once
#include "common.h"
bool snow_audio_init(void);
void snow_audio_exit(void);
void snow_audio_play(const char *name, bool loop);
void snow_audio_stop_walk(void);
void snow_audio_mute(bool muted);
