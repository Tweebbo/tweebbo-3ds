#include "frame_pacing.h"
#include "world_activity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  FramePacing p = {0};
  unsigned warm = 0, quiet = 0, busy = 0;
  for (unsigned tick = 0; tick < 600; tick++) {
    u64 now = (u64)tick * 1000 / 60;
    bool draw = frame_pacing_draw(&p, now, tick >= 480);
    if (tick < 120) warm += draw;
    else if (tick < 480) quiet += draw;
    else busy += draw;
  }
  assert(warm == 120 && quiet == 180 && busy == 120);
  assert(frame_pacing_draw(&p, 10000, true));
  assert(frame_pacing_draw(&p, 11000, false));
  assert(frame_pacing_draw(&p, 12000, false));
  assert(!frame_pacing_draw(&p, 12001, false));
  assert(frame_pacing_draw(&p, 12002, true)); /* input never waits for the idle interval */
  assert(frame_pacing_draw(&p, 1, false)); /* clock reset */
  assert(frame_pacing_draw(&p, 100000, false)); /* resume after suspend */
  FramePacing slow = {0};
  for (u64 now = 0; now < 20000; now += 40) assert(frame_pacing_draw(&slow, now, false));

  memset(&W, 0, sizeof W);
  assert(world_animation_active(5000)); /* connection screen */
  W.in_room = true;
  assert(!world_animation_active(5000));
  Ent *e = &W.ents[MAX_ENTS - 1];
  *e = (Ent){.used=true, .x=2, .fx=2, .y=3, .fy=3, .z=256, .fz=256, .t0=5000};
  assert(!world_animation_active(5000)); /* standing on raised furniture */
  e->fx = 1; assert(world_animation_active(5100));
  assert(!world_animation_active(5500));
  e->fx=2; e->fz=0; assert(world_animation_active(5100));
  e->fz=256;
  const unsigned flags[] = {FLAG_WAVE, FLAG_DANCE, FLAG_ROLL};
  for (unsigned i=0; i<sizeof flags/sizeof *flags; i++) {
    e->flags=flags[i]; assert(world_animation_active(6000));
  }
  e->flags=FLAG_SIT; assert(!world_animation_active(6000));
  e->flags=FLAG_LAY; assert(!world_animation_active(6000));
  e->talk_until=6100; assert(world_animation_active(6000)); e->talk_until=0;
  e->typing_until=6100; assert(world_animation_active(6000)); e->typing_until=0;
  e->drink_until=6100; assert(world_animation_active(6000)); e->drink_until=0;
  e->action=4; e->action_until=6100; assert(world_animation_active(6000));
  assert(!world_animation_active(6100));
  e->action_until=0; assert(world_animation_active(6100));
  e->used=false; assert(!world_animation_active(6100));
  puts("PASS: 360 quiet ticks produce 180 frames; active ticks all draw; immediate input, suspend, slow frames and room activity covered.");
}
