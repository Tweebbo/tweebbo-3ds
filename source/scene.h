// Top screen: the room at native resolution (64×32 per tile).
#pragma once
#include "common.h"

#define TOP_W 400
#define TOP_H 240

void scene_room_changed(u64 now);
void scene_chat(u32 eid, const char *text, u64 now);
void scene_draw(u64 now);
/** Toggles between 1:1 following the avatar and the overview. */
void scene_toggle_overview(void);
bool scene_overview(void);
/** Water tile in the pool. */
bool scene_water_tile(int x, int y);
void scene_mark_destination(int x, int y, u64 now);
void scene_select(u32 eid);
u32 scene_selected(void);
/** Pixel-art handheld over 3DS players (12×13 px × k). */
void console_badge(float x, float y, float k);
/** Badge of a native device (DEVICE_*): 3DS 12×13, Switch 14×9, Vita 12×9. */
void device_badge(int device, float x, float y, float k);
void device_badge_size(int device, int *w, int *h);

/** Ghost furniture in build mode (like renderer.ghost on the web). */
typedef struct {
  bool on, wall, ok;
  u16 def;
  int x, y, rot, z;
  u32 ignore; /* item being moved (not drawn in its old place) */
} Ghost;
extern Ghost G;
int scene_avatar_count(void);
/** "Ⓐ action" prompt at the bottom of the top screen (NULL hides it). */
void scene_prompt(const char *text);

u64 scene_look(u32 eid);
