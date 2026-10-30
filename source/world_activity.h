#pragma once
#include "world.h"
#include "proto.h"

static inline bool world_animation_active(u64 now) {
  if (!W.in_room) return true;
  for (int i = 0; i < MAX_ENTS; i++) {
    const Ent *e = &W.ents[i];
    if (!e->used) continue;
    if (ent_moving(e, now) || (now < e->t0 + MOVE_MS && e->fz != e->z) ||
        (e->flags & (FLAG_WAVE | FLAG_DANCE | FLAG_ROLL)) ||
        now < e->talk_until || now < e->typing_until || now < e->drink_until ||
        (e->action && (!e->action_until || now < e->action_until))) return true;
  }
  return false;
}
