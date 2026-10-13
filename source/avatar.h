// Avatars composed from parts, like client/src/avatar.ts.
#pragma once
#include "common.h"

typedef enum { POSE_STD, POSE_WALK, POSE_SIT, POSE_FLOOR, POSE_WAVE, POSE_DANCE, POSE_LAY, POSE_SWIM, POSE_FLOAT } Pose;
enum { EXPR_TALK = 1, EXPR_BLINK = 2 };

typedef struct {
  int skin, hair_style, hair_color, shirt_color, shirt_style, pants, shoes, pants_style, shoe_style, face_style;
} Look;

#define SPRITE_W 64
#define SPRITE_H 112
#define ANCHOR_X 32
#define ANCHOR_Y 98
#define SEATED_HIP 32
/** Swimming canvas (160×112), like swimmerSprite() in avatar.ts. */
#define SWIM_ANCHOR_X 80

extern const u32 SKIN[4], HAIR[8], SHIRT[16], PANTS[8], SHOES[4];

Look look_decode(u64 n);
u64 look_encode(const Look *l);
/** Shirt color as 0xRRGGBB (chat bubble border). */
u32 look_shirt_rgb(u64 look);
/**
 * Draws the avatar with the top-left corner of its virtual canvas
 * (64×112, or 89×70 lying down) at (x, y). `alpha` 0–255.
 */
void avatar_draw(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, int drink, bool sip,
                 float x, float y, float scale, u8 alpha);
/** Head only (hair, face and eyes), for lists of people. Same canvas as avatar_draw. */
void avatar_draw_head(u64 look, int dir, float x, float y, float scale);
/** Avatar swimming or floating (pool): normal head plus the swimming body under a water veil. */
void avatar_draw_swim(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, float x, float y, float scale);
