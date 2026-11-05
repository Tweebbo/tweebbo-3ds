#pragma once
#include "common.h"
#include "assets.h"

typedef struct {u8 page;u16 x,y,w,h;} AvatarFxImage;
typedef struct {u16 image;s16 x,y;s8 depth;u8 ink,alpha;} AvatarFxLayer;
typedef struct {u32 hide;u8 action[3],frame[3];} AvatarFxBody;
typedef struct {u32 first;u16 count,body;s16 dx,dy;} AvatarFxFrame;
typedef struct {unsigned sprite;s16 x,y;u32 color;u8 scale;u32 overlay;} AvatarFxDraw;
typedef struct {u8 *pixels;int x,y,width,height;} AvatarFxBitmap;
typedef struct {
  u16 id,period,moving_period;
  const u16 *still,*moving;
  AvatarFxLayer shadow[8];
  u32 foreground,background;
  bool transform;
} AvatarFxAnimation;

extern const AvatarFxImage avatar_fx_images[];
extern const AvatarFxLayer avatar_fx_layers[];
extern const AvatarFxBody avatar_fx_bodies[];
extern const AvatarFxFrame avatar_fx_frames[];
extern const AvatarFxAnimation avatar_fx_animations[];
extern const unsigned avatar_fx_animation_count,avatar_fx_body_count;

const AvatarFxAnimation *avatar_effect(u16 id);
const AvatarFxFrame *avatar_effect_frame(u16 id,int direction,u64 tick,bool moving);
void avatar_effect_layers(u16 id,int direction,u64 tick,bool moving,float x,float y,float scale,bool behind);
bool avatar_effect_shadow(u16 id,int direction,float x,float y,float scale);
/** Compose the whole colored figure before its appearance transform. The caller owns pixels. */
bool avatar_effect_compose(const AvatarFxDraw *parts,unsigned count,bool mirrored,u16 effect,AvatarFxBitmap *bitmap);
bool avatar_effect_penguin_part(unsigned frame,bool body,u32 color,AvatarFxDraw *part);
void avatar_effect_pixels_close(void);
