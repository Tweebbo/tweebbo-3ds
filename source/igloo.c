#include "igloo.h"
#include "gfx.h"
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
  for (int layer = 0; layer < 2; layer++) {
    char name[48];
    snprintf(name, sizeof name, "penguin-%u-%s", frame, layer ? "body" : "base");
    const SceneArt *a = gfx_art(name);
    u32 c = layer ? colors[variant] : 0xffffff;
    u32 abgr = 0xff000000u | (c & 255) << 16 | (c & 0xff00) | c >> 16;
    if (a) gfx_art_draw(a, x - a->rx * scale, y - a->ry * scale, scale, scale, abgr);
  }
  return true;
}
