#include "igloo.h"
#include "gfx.h"
#include "avatar_effects.h"
#include "gen/igloo.h"
#include <stdio.h>
#include <string.h>

bool igloo_penguin(u32 guest) { return guest >= 4001000200u && guest < 4001000208u; }

bool igloo_draw(const Ent *e, int dir, Pose pose, u64 now, float x, float y, float zoom) {
  if (strcmp(W.model, "igloo_waddle") || !igloo_penguin(e->guest)) return false;
  static const int directions[] = {6,7,8,1,2,3,4,5};
  static const u32 colors[] = {0x2355cd,0xd04780,0xd5532b,0x2d9c69,0x8e48b6,0x2f9bb4,0xb89229,0x5f5b9b};
  unsigned variant = e->guest - 4001000200u;
  int clip = directions[dir & 7];
  if (pose == POSE_WALK) clip += 8;
  else if (pose == POSE_SIT || pose == POSE_FLOOR) clip += 16;
  else if (pose == POSE_WAVE) clip = 25;
  else if (pose == POSE_DANCE) clip = 26;
  const unsigned short *frames = penguin_clips[clip];
  unsigned frame = frames[1 + ((now + variant * 410) * PENGUIN_FPS / 1000) % frames[0]];
  float scale = .75f * zoom;
  bool moving=pose==POSE_WALK;
  const AvatarFxAnimation *effect=avatar_effect(e->effect);
  const AvatarFxFrame *effect_frame=e->effect&&pose!=POSE_LAY&&pose!=POSE_SWIM&&pose!=POSE_FLOAT?avatar_effect_frame(e->effect,dir,now/80,moving):NULL;
  if(e->effect){
    if(pose!=POSE_LAY&&pose!=POSE_SIT&&pose!=POSE_FLOOR)avatar_effect_shadow(e->effect,dir,x,y,zoom);
    avatar_effect_layers(e->effect,dir,now/80,moving,x,y,zoom,true);
  }
  float body_x=x+(effect_frame?effect_frame->dx:0)*zoom,body_y=y+(effect_frame?effect_frame->dy:0)*zoom;
  AvatarFxDraw parts[2];
  bool transformed=effect&&effect->transform&&avatar_effect_penguin_part(frame,false,0xffffff,&parts[0])&&avatar_effect_penguin_part(frame,true,colors[variant],&parts[1])
    &&gfx_avatar_effect(0x4000000000000000ull|((u64)variant<<32)|frame,e->effect,parts,2,false,body_x,body_y,scale,255);
  if(!transformed){
  for (int layer = 0; layer < 2; layer++) {
    char name[48];
    snprintf(name, sizeof name, "penguin-%u-%s", frame, layer ? "body" : "base");
    const SceneArt *a = gfx_art(name);
    u32 c = layer ? colors[variant] : 0xffffff;
    u32 abgr = 0xff000000u | (c & 255) << 16 | (c & 0xff00) | c >> 16;
    if (a) gfx_art_draw(a, body_x - a->rx * scale, body_y - a->ry * scale, scale, scale, abgr);
  }
  }
  if(e->effect)avatar_effect_layers(e->effect,dir,now/80,moving,x,y,zoom,false);
  return true;
}
