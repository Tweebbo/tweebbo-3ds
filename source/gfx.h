// Exported textures (atlas and room backgrounds) and drawing primitives.
#pragma once
#include <citro2d.h>
#include "assets.h"

bool gfx_init(void);
void gfx_exit(void);
/** Region of an atlas page drawn at (x, y). `flip` mirrors horizontally in place. */
void gfx_sprite(int page, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color);
/** Furniture layer: loads its texture from romfs:/furni on demand (LRU cache). */
void gfx_furni(u16 def, const FurniPart *part, float x, float y, float scale, u32 color);
/** Catalogue icon of a furniture item, centered on (cx, cy) and shrunk to fit `box`. */
void gfx_furni_icon(u16 def, float cx, float cy, float box);
/** Preloads a furniture texture (when entering a room). */
void gfx_furni_preload(u16 def);
/** Pet part with its breed palette; `solid` paints it a single color (shadow). */
void gfx_pet(int palette, const PetPart *p, float x, float y, float scale, bool flip, u32 color, bool solid);
/** Call once per frame: frees old textures when over budget. */
void gfx_frame(void);
void gfx_filter(bool linear);
void gfx_additive(bool on);
/** Pre-rendered background of a classic room; NULL if not packed. */
const RoomBackground *gfx_room(const char *model);
/** Frees the loaded room background. */
void gfx_room_release(void);
void gfx_room_draw(const RoomBackground *bg, float x, float y, float scale);
/** Loads the pages of a layered public room (freeing the previous one). NULL if it has none. */
const Scene *gfx_scene(const char *model);
void gfx_scene_sprite(const SceneSprite *s, float x, float y, float zoom, u8 alpha);
/** Second pass in a solid color with alpha (teal water veil, like source-atop on the web). */
void gfx_sprite_solid(int page, int sx, int sy, int sw, int sh, float x, float y, float sx_scale, float sy_scale, bool flip, u32 color);
/** Like gfx_sprite with separate x and y scales. */
void gfx_sprite_xy(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color);

// Text with the system font.
void text_begin(void);
/** Draws and returns the width in pixels. `align`: 0 left, 1 center, 2 right. */
float text_draw(const char *s, float x, float y, float size, u32 color, int align);
float text_width(const char *s, float size);
/** Cuts `s` with "…" so it does not exceed `max` pixels. */
void text_fit(char *s, float size, float max);

void fill_rect(float x, float y, float w, float h, u32 color);
void fill_round(float x, float y, float w, float h, float r, u32 color);
