#pragma once
#include "common.h"
bool game_audio_load(int bank);
void game_audio_free(void);
void game_audio_play(const char *name,int channel,bool loop);
void game_audio_stop(void);
void game_audio_toggle(void);
bool game_audio_muted(void);
