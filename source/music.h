// Room playlist on the console: the Jukebox / Trax machine queue of the room (server
// room/music.rs, s2c MUSIC 142 / c2s MUSIC 23, FEATURE_MUSIC). Native Trax songs play on the
// DSP from the original samples, downloaded per song from /static/trax/3ds and cached on the
// SD card; YouTube and Spotify tracks only show their title (they play on PC and phones).
#pragma once
#include "common.h"

void music_receive(const char *json, u64 now);
void music_room_changed(void);
/** Every frame: downloads and schedules the samples of the playing Trax song. */
void music_poll(u64 now);
bool music_machine(u16 def);
/** A music machine within reach: "Press A" opens the playlist. */
bool music_context(char *label, int cap, u64 now);
void music_draw(u64 now);
/** The room has a Jukebox or Trax machine: the room panel offers its playlist even when silent. */
bool music_in_room(void);
/** One line for the room panel while music plays ("" when silent). */
const char *music_now_playing(void);
void music_exit(void);
