#pragma once
#include "common.h"

typedef struct {
  u64 last_tick, last_active, last_draw;
  bool started;
} FramePacing;

/* Call after input and simulation, once per display tick. A quiet room still animates
   at about 30 fps; input and network processing keep the normal polling cadence. */
static inline bool frame_pacing_draw(FramePacing *p, u64 now, bool active) {
  if (!p->started || now < p->last_tick || now - p->last_tick > 1000) {
    p->started = true;
    p->last_active = p->last_draw = p->last_tick = now;
    return true;
  }
  p->last_tick = now;
  if (active) p->last_active = now;
  if (now - p->last_active < 2000 || now - p->last_draw >= 30) {
    p->last_draw = now;
    return true;
  }
  return false;
}
