// Showcase mode: the five brand character models rotate with SET_LOOK, so every
// console in the room (web, 3DS, Switch, Vita) sees the same character change.
#include "showcase.h"
#include <stdio.h>
#include "avatar.h"
#include "gfx.h"
#include "net.h"
#include "proto.h"
#include "scene.h"
#include "world.h"

#ifdef TWEEBBO_SHOWCASE

#define LOOK_MS 5000
#define GESTURE_MS 11000
#define WALK_MS 9000

/* The showcase cast, from the brand presets in Tweebbo-designs/preset_*.png.
   Hair and clothing tints multiply the sprite, so the models use the light (grey)
   wardrobe sprites; the dark "hair mask" styles keep their own colour, like the web.
   Indices are clamped by avatar_compose when a wardrobe has fewer entries. */
static const Look CAST[] = {
  /* Bobba: short blond hair, white tee, dark blue trousers, white shoes. */
  {.skin = 0, .hair_style = 1, .hair_color = 3, .shirt_color = 0, .shirt_style = 0, .pants = 0,
   .shoes = 0, .pants_style = 0, .shoe_style = 0, .face_style = 0},
  /* Cyber: black spiky hair, black shirt and trousers, sneakers. */
  {.skin = 0, .hair_style = 5, .hair_color = 0, .shirt_color = 1, .shirt_style = 3, .pants = 1,
   .shoes = 1, .pants_style = 1, .shoe_style = 1, .face_style = 9},
  /* Director: dark hair, black formal top and trousers, classic shoes. */
  {.skin = 1, .hair_style = 12, .hair_color = 0, .shirt_color = 1, .shirt_style = 3, .pants = 1,
   .shoes = 0, .pants_style = 1, .shoe_style = 0, .face_style = 10},
  /* Street: brown spikes, green sweater, dark jeans, brown shoes. */
  {.skin = 0, .hair_style = 6, .hair_color = 1, .shirt_color = 5, .shirt_style = 1, .pants = 0,
   .shoes = 1, .pants_style = 0, .shoe_style = 2, .face_style = 9},
  /* VIP Girl: brown bob, purple top and trousers, white shoes. */
  {.skin = 1, .hair_style = 3, .hair_color = 1, .shirt_color = 9, .shirt_style = 1, .pants = 6,
   .shoes = 2, .pants_style = 6, .shoe_style = 2, .face_style = 1},
};
static const char *CAST_NAME[] = {"Bobba", "Cyber", "Director", "Street", "VIP Girl"};

static struct {
  u64 next_look, next_gesture, next_walk;
  int cast;
} S;

void showcase_start(u64 now) {
  S.cast = 0;
  S.next_look = now + 2000;
  S.next_gesture = now + 7000;
  S.next_walk = now + WALK_MS;
}

void showcase_update(u64 now) {
  // Online or in the offline simulation (same protocol): the cast keeps rotating either way.
  if (!W.in_room || !(net_online() || net_state() == NET_OFFLINE)) return;
  if (now >= S.next_look) {
    S.next_look = now + LOOK_MS;
    S.cast = (S.cast + 1) % COUNT(CAST);
    send_look(look_encode(&CAST[S.cast]));
  }
  if (now >= S.next_gesture) {
    S.next_gesture = now + GESTURE_MS;
    send_gesture(S.cast % 2 ? GESTURE_WAVE : GESTURE_DANCE);
  }
  if (now >= S.next_walk) {
    S.next_walk = now + WALK_MS;
    Ent *me = world_ent(W.me);
    if (me) {
      int x = me->x + rnd_range(-3, 4), y = me->y + rnd_range(-2, 3);
      if (x >= 0 && y >= 0 && x < W.w && y < W.h && world_tile(x, y)) send_walk(x, y);
    }
  }
}

void showcase_top(u64 now) {
  (void)now;
  char line[64];
  snprintf(line, sizeof line, "Showcase model: %s", CAST_NAME[S.cast]);
  float w1 = text_width("TWEEBBO SHOWCASE", 13), w2 = text_width(line, 11);
  float w = w1 > w2 ? w1 : w2;
  fill_round(6, 6, w + 16, 50, 4, C2D_Color32(0x0b, 0x0d, 0x0e, 190));
  text_draw("TWEEBBO SHOWCASE", 14, 9, 13, C2D_Color32(0xff, 0xd1, 0x66, 255), 0);
  text_draw(net_state() == NET_OFFLINE ? "Nintendo 3DS - offline" : "Nintendo 3DS - LAN hotel", 14, 25, 11, C2D_Color32(0xe8, 0xea, 0xe2, 255), 0);
  text_draw(line, 14, 39, 11, C2D_Color32(0x8f, 0xd9, 0xff, 255), 0);
}

#endif
