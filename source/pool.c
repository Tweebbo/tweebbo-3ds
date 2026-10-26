#include "language.h"
// Lido activities on the console: the server decides everything (server/src/room/activities.rs,
// diving.rs); this file reads its ACTIVITY state, offers the actions with A and draws the original
// game artwork packed into the scene pages (tools/pack.py SCENE_ART), like the web edition's
// client/src/activities.ts, activity-scene.ts and activity-art.ts.
#include "pool.h"
#include "ui_int.h"
#include "avatar.h"
#include "gfx.h"
#include "net.h"
#include "scene.h"
#include <citro2d.h>
#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SWIMMERS 64
#define MAX_QUEUE 16

typedef struct { u32 eid; s8 color; bool booth; u8 bx, by; } Swimmer;
typedef struct { u32 eid; int position, balance, action; bool rebalanced, hit; s64 last; } Wobbler;
static struct {
  bool known;
  s64 offset;                  /* server clock - local clock (ms) */
  Swimmer swimmers[MAX_SWIMMERS];
  int swimmer_n;
  u32 dive_queue[MAX_QUEUE], wobble_queue[MAX_QUEUE];
  int dive_queue_n, wobble_queue_n;
  struct {
    bool on; u32 eid; int phase, score; s64 started, until; bool voted;
    struct { int x, y, action, frame; bool lower, airborne, direction; } m;
  } dive;
  struct { bool on; int phase, winner; s64 starts, ends, result_until; Wobbler p[2]; } wob;
  /* Local: the jump was started, a signpost walk is pending, the big splash. */
  bool dive_started;
  s64 started_key;
  int area_x, area_y;
  u64 area_at;
  char area_target[16], arrive_from[16];
  s64 splash_key; float splash_x, splash_y; u64 splash_at;
} P;

static s64 server_now(u64 now) { return (s64)now + P.offset; }
static bool model_is(const char *m) { return !strcmp(W.model, m); }
bool pool_room(void) { return model_is("pool_a") || model_is("pool_b") || model_is("md_a"); }

static void command(const char *json) { send_activity(json); }

static Swimmer *swimmer(u32 eid) {
  for (int i = 0; i < P.swimmer_n; i++) if (P.swimmers[i].eid == eid) return &P.swimmers[i];
  return NULL;
}
int pool_swimsuit(u32 eid) { Swimmer *s = swimmer(eid); return s ? s->color : -1; }
static bool queued(const u32 *q, int n, u32 eid) { for (int i = 0; i < n; i++) if (q[i] == eid) return true; return false; }

void pool_room_changed(void) {
  memset(&P.swimmers, 0, sizeof P.swimmers);
  P.swimmer_n = P.dive_queue_n = P.wobble_queue_n = 0;
  P.dive.on = P.wob.on = P.known = false;
  P.dive_started = false;
  P.area_target[0] = 0;
  if (pool_room() && P.arrive_from[0]) {
    // Arrived through a Lido signpost: the server places the figure at the matching sign.
    char json[64];
    snprintf(json, sizeof json, "{\"op\":\"arrive\",\"from\":\"%s\"}", P.arrive_from);
    command(json);
  }
  P.arrive_from[0] = 0;
}

static int num(json_t *o, const char *k) { return (int)json_integer_value(json_object_get(o, k)); }
static s64 num64(json_t *o, const char *k) { return (s64)json_integer_value(json_object_get(o, k)); }

void pool_receive(const char *text, u64 now) {
  json_t *j = json_loads(text, 0, NULL);
  json_t *st = json_object_get(j, "state");
  if (!json_is_object(st)) { json_decref(j); return; }
  P.offset = num64(j, "now") - (s64)now;
  P.known = true;
  // swimmers: {"<eid>": {"color": n|null, "booth": [x, y]|null, ...}}
  P.swimmer_n = 0;
  const char *key; json_t *v;
  json_object_foreach(json_object_get(st, "swimmers"), key, v) {
    if (P.swimmer_n >= MAX_SWIMMERS) break;
    Swimmer *s = &P.swimmers[P.swimmer_n++];
    s->eid = (u32)strtoul(key, NULL, 10);
    json_t *c = json_object_get(v, "color"), *b = json_object_get(v, "booth");
    s->color = json_is_integer(c) ? (s8)json_integer_value(c) : -1;
    s->booth = json_is_array(b);
    s->bx = s->booth ? (u8)json_integer_value(json_array_get(b, 0)) : 0;
    s->by = s->booth ? (u8)json_integer_value(json_array_get(b, 1)) : 0;
  }
  size_t i;
  P.dive_queue_n = P.wobble_queue_n = 0;
  json_array_foreach(json_object_get(st, "dive_queue"), i, v) if (P.dive_queue_n < MAX_QUEUE) P.dive_queue[P.dive_queue_n++] = (u32)json_integer_value(v);
  json_array_foreach(json_object_get(st, "wobble_queue"), i, v) if (P.wobble_queue_n < MAX_QUEUE) P.wobble_queue[P.wobble_queue_n++] = (u32)json_integer_value(v);
  json_t *d = json_object_get(st, "dive");
  P.dive.on = json_is_object(d);
  if (P.dive.on) {
    P.dive.eid = (u32)num(d, "eid"); P.dive.phase = num(d, "phase"); P.dive.score = num(d, "score");
    P.dive.started = num64(d, "started"); P.dive.until = num64(d, "until");
    char me[16]; snprintf(me, sizeof me, "%u", (unsigned)W.guest_id);
    P.dive.voted = json_object_get(json_object_get(d, "votes"), me) != NULL;
    json_t *m = json_object_get(d, "motion");
    P.dive.m.x = num(m, "x"); P.dive.m.y = num(m, "y"); P.dive.m.action = num(m, "action"); P.dive.m.frame = num(m, "frame");
    P.dive.m.lower = json_is_true(json_object_get(m, "lower")); P.dive.m.airborne = json_is_true(json_object_get(m, "airborne"));
    P.dive.m.direction = json_is_true(json_object_get(m, "direction"));
  }
  json_t *w = json_object_get(st, "wobble");
  P.wob.on = json_is_object(w);
  if (P.wob.on) {
    P.wob.phase = num(w, "phase"); P.wob.starts = num64(w, "starts"); P.wob.ends = num64(w, "ends");
    P.wob.result_until = num64(w, "result_until");
    json_t *win = json_object_get(w, "winner");
    P.wob.winner = json_is_integer(win) ? (int)json_integer_value(win) : -1;
    for (int k = 0; k < 2; k++) {
      json_t *p = json_array_get(json_object_get(w, "players"), k);
      Wobbler *o = &P.wob.p[k];
      o->eid = (u32)num(p, "eid"); o->position = num(p, "position"); o->balance = num(p, "balance"); o->action = num(p, "action");
      o->rebalanced = json_is_true(json_object_get(p, "rebalanced")); o->hit = json_is_true(json_object_get(p, "hit"));
      o->last = num64(p, "last");
    }
  }
  // jumpingPlaceOk: at the top of the tower the jump starts straight away.
  if (P.dive.on && P.dive.eid == W.me && P.dive.phase == 0 && !P.dive_started) { P.dive_started = true; command("{\"op\":\"dive\"}"); }
  if (!P.dive.on || P.dive.eid != W.me) P.dive_started = false;
  json_decref(j);
}

u64 pool_look(const Ent *e, u64 look) {
  int color = pool_swimsuit(e->id);
  if (color < 0 || !pool_room()) return look;
  Look L = look_decode(look & ((1ull << 42) - 1));
  L.shirt_color = color;
  return look_encode(&L) | (look & ~((1ull << 42) - 1)) | AV_BATHING;
}

bool pool_hides(const Ent *e) {
  if (P.dive.on && P.dive.eid == e->id && P.dive.phase < 2) return true;
  if (P.wob.on) for (int k = 0; k < 2; k++) if (P.wob.p[k].eid == e->id) return true;
  return false;
}

bool pool_switch_closed(int k) {
  if (k == 2) return P.dive.on && P.dive.phase < 2;
  int bx = model_is("pool_a") ? 17 : 8 + k, by = model_is("pool_a") ? (k == 0 ? 11 : 9) : 1;
  for (int i = 0; i < P.swimmer_n; i++) if (P.swimmers[i].booth && P.swimmers[i].bx == bx && P.swimmers[i].by == by) return true;
  return false;
}

// ---------------------------------------------------------------- artwork

/** Zero point of the pole and the countdown (public-rooms md_a.paalu). */
static const float PAALU_ZERO[2] = {206, 432}, PAALU_COUNTER[2] = {234, 328};
/** Paalu Player - Head offsets for each balance pose. */
static const int WOBBLE_HEAD[5][2] = {{-4, 2}, {-2, 1}, {0, 0}, {2, 1}, {4, 2}};

static const SceneArt *art(const char *name) { return gfx_art(name); }
static const SceneArt *art_first(const char *a, const char *b, const char *c) {
  const SceneArt *s = art(a);
  if (!s && b) s = art(b);
  if (!s && c) s = art(c);
  return s;
}
/** drawArt(c, name, x, y, 2): the registration point at world (x, y), enlarged twice. */
static void art_world(const char *name, float wx, float wy, u32 color) {
  const SceneArt *a = art(name);
  if (!a) return;
  float x, y, z = scene_zoom();
  scene_to_screen(wx - a->rx * 2, wy - a->ry * 2, &x, &y);
  gfx_art_draw(a, x, y, 2 * z, 2 * z, color);
}

static u32 rgb_tint(u32 rgb, u8 alpha) { return C2D_Color32(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, alpha); }
static u32 skin_of(u64 look) { Look L = look_decode(look & ((1ull << 42) - 1)); return SKIN[L.skin & 3]; }
static u32 suit_of(u64 look) { Look L = look_decode(look & ((1ull << 42) - 1)); return SHIRT[L.shirt_color & 15]; }
static bool female(u64 look) { return look_gender(look & ((1ull << 42) - 1)) == 'F'; }

/** A small body part drawn into the current view at its buffer point (bx, by), twice its size. */
static void part2(const SceneArt *a, float bx, float by, u32 rgb) {
  if (a) gfx_art_draw(a, (bx - a->rx) * 2, (by - a->ry) * 2, 2, 2, rgb_tint(rgb, 255));
}

/** Paalu Player Class: body parts of the balance pose and the large head (80x116 view units). */
static void draw_wobbler(u64 look, int dir, int level, const char *action, int frame, bool hit_shift, float wx, float wy) {
  float sx, sy, z = scene_zoom();
  scene_to_screen(wx, wy, &sx, &sy);
  C2D_ViewTranslate(sx, sy);
  C2D_ViewScale(z, z);
  const char *model = female(look) ? "s01" : "s02";
  static const char *front[] = {"lh", "bd", "rh"}, *back[] = {"rh", "bd", "lh"};
  for (int k = 0; k < 3; k++) {
    const char *p = dir == 4 ? front[k] : back[k];
    char a[48], b[48], c[48];
    for (int pass = 0; pass < (strcmp(p, "bd") ? 1 : 2); pass++) {
      const char *part = pass ? "ch" : p, *m = pass ? model : "s01";
      snprintf(a, sizeof a, "shp_%s_%d_%s_%s_%d_%d", action, level, part, m, dir, frame);
      snprintf(b, sizeof b, "shp_%s_%d_%s_%s_%d_0", action, level, part, m, dir);
      snprintf(c, sizeof c, "shp_std_%d_%s_%s_%d_0", level, part, m, dir);
      part2(art_first(a, b, c), 6, 48, pass ? suit_of(look) : skin_of(look));
    }
  }
  int dx = dir == 4 ? 1 : -7, dy = dir == 4 ? 0 : 1;
  int hx = hit_shift ? (dir == 4 ? -2 : 2) : 0, hy = hit_shift ? (dir == 4 ? 1 : -1) : 0;
  avatar_draw_head(look & ~AV_BATHING, dir == 4 ? 3 : 7, 12 + (dx + WOBBLE_HEAD[level][0] + hx) * 2, (dy + WOBBLE_HEAD[level][1] + hy) * 2, 1);
  C2D_ViewReset();
}

/** Jumping Pelle Class around its centre (x, y): flipped horizontally going up, vertically coming down. */
static void draw_diver(u64 look, int action, bool down, int frame, float sx, float sy, float scale, bool running) {
  static const char *moves[] = {"jmp", "a", "d", "w", "e", "z", "x", "s", "jmp"};
  char move[8];
  if (running) snprintf(move, sizeof move, "%s", action == 1 ? "run" : "std");
  else if (action == 0 || action == 8) snprintf(move, sizeof move, "jmp");
  else snprintf(move, sizeof move, "j%c%s", down ? 'd' : 'u', moves[action < 9 ? action : 0]);
  static const int RUN[8] = {0, 1, 2, 3, 3, 2, 1, 0};
  int f = !strcmp(move, "run") ? RUN[frame & 7] : frame & 7;
  int direction = !strncmp(move, "jd", 2) ? 0 : 2;
  C2D_ViewTranslate(sx, sy);
  C2D_ViewScale(down ? scale : -scale, down ? -scale : scale);
  C2D_ViewTranslate(-60, -60);
  static const char *parts[] = {"bd", "lh", "ch", "rh"};
  for (int k = 0; k < 4; k++) {
    const char *m = !strcmp(parts[k], "ch") ? (female(look) ? "s01" : "s02") : "s01";
    char a[48], b[48], c[48];
    snprintf(a, sizeof a, "sh_%s_%s_%s_%d_%d", move, parts[k], m, direction, f);
    snprintf(b, sizeof b, "sh_%s_%s_%s_%d_0", move, parts[k], m, direction);
    snprintf(c, sizeof c, "sh_std_%s_%s_2_0", parts[k], m);
    part2(art_first(a, b, c), 10, 52, !strcmp(parts[k], "ch") ? suit_of(look) : skin_of(look));
  }
  avatar_draw_head(look & ~AV_BATHING, down ? 0 : 2, 20, 8, 1);
  C2D_ViewReset();
}

static u64 ent_look(u32 eid) { Ent *e = world_ent(eid); return e ? pool_look(e, e->look) : 0; }

static int wobble_level(int balance) { int l = balance / 20 + 2; return l < 0 ? 0 : l > 4 ? 4 : l; }

void pool_draw_room(u64 now) {
  if (!pool_room() || !P.known) return;
  s64 t = server_now(now);
  if (P.wob.on && model_is("md_a")) {
    s64 fall = P.wob.result_until - 1500, last_move = P.wob.p[0].last > P.wob.p[1].last ? P.wob.p[0].last : P.wob.p[1].last;
    for (int i = 0; i < 2; i++) {
      Wobbler *p = &P.wob.p[i];
      if (!world_ent(p->eid)) continue;
      int dir = i == 0 ? 0 : 4;
      u64 look = ent_look(p->eid);
      float x = PAALU_ZERO[0] + p->position * 16 + 4, y = PAALU_ZERO[1] - p->position * 8 - 96;
      if (P.wob.phase == 2 && P.wob.winner != i) {
        // Paalu Player Class drop: a hop drifting sideways, then the splash in the pool.
        s64 dt = t - fall; if (dt < 0) dt = 0;
        int frame = (int)(dt / 50), max = p->balance < 0 ? 28 : 38, side = p->balance < 0 ? -1 : 1;
        if (2 * frame < max) draw_wobbler(look, dir, side < 0 ? 0 : 4, "drp", 0, false, x + side * frame * 2, roundf(y - 100 * sinf(2 * frame / 10.0f)));
        else {
          int splash = (int)((dt - max * 25) / 66);
          if (splash < 10) { char n[16]; snprintf(n, sizeof n, "splash_%d", splash); art_world(n, x - 4 + side * 32, y + 96 + side * 16, 0xffffffff); }
        }
        continue;
      }
      s64 age = t - p->last;
      const char *action = (p->action == 3 || p->action == 4) && age < 300 ? (p->action == 3 ? "hit1" : "hit2") : (p->action == 5 || p->action == 6) && age < 400 ? "wlk" : "std";
      int frame = !strcmp(action, "std") ? 0 : (int)(age / 100) % 2;
      draw_wobbler(look, dir, wobble_level(p->balance), action, frame, strcmp(action, "std") && strcmp(action, "wlk") && age < 100, x, y);
      if (p->hit && t - last_move < 330) {
        char n[24]; snprintf(n, sizeof n, "paalu hit %d %d", dir, (int)(last_move % 4) + 1);
        art_world(n, x + 36 + (dir == 0 ? -48 : 48), y + 96 - 8 - 16, 0xffffffff);
      }
    }
    if (P.wob.phase == 1 && t < P.wob.starts) {
      // Peelo Counter Class: three animation frames at the start of each second.
      s64 left = P.wob.starts - t; int count = (int)((left + 999) / 1000), into = (int)(count * 1000 - left), frame = 1 + into / 66;
      if (frame > 3) frame = 3;
      if (count >= 1 && count <= 3) { char n[24]; snprintf(n, sizeof n, "startcount %d %d", count, frame); art_world(n, PAALU_COUNTER[0], PAALU_COUNTER[1], 0xffffffff); }
    }
  }
  if (P.dive.on && model_is("pool_b")) {
    Ent *e = world_ent(P.dive.eid);
    if (e && P.dive.phase == 1 && P.dive.m.lower) {
      float x, y;
      scene_to_screen((P.dive.m.x / 100.0f - 216) * 2, (P.dive.m.y / 100.0f - 30) * 2, &x, &y);
      draw_diver(ent_look(P.dive.eid), P.dive.m.action, P.dive.m.direction, P.dive.m.frame, x, y, scene_zoom(), false);
      // Jumping Pelle Class: 40 pixels above the water the big splash is placed and shown 250 ms later.
      if (P.splash_key != P.dive.started && P.dive.m.y >= 31000 + (42900 - P.dive.m.x) / 2 - 4000) {
        P.splash_key = P.dive.started; P.splash_x = P.dive.m.x / 100.0f - 15; P.splash_y = P.dive.m.y / 100.0f - 15; P.splash_at = now + 250;
      }
    }
    if (P.splash_at) {
      int frame = (int)(((s64)now - (s64)P.splash_at) * 30 / 1000);
      if (frame >= 20) P.splash_at = 0;
      else if (frame >= 0) { char n[20]; snprintf(n, sizeof n, "big_splash_%d", frame); art_world(n, (P.splash_x - 216) * 2, (P.splash_y - 30) * 2, C2D_Color32(255, 255, 255, 153)); }
    }
  }
}

bool pool_tower_view(void) {
  return model_is("pool_b") && P.dive.on && P.dive.eid == W.me && P.dive.phase < 2 && !P.dive.m.lower;
}

/** pool_tower.room at the original size (half the room's world units): sky, clouds, tower, board, jumper. */
void pool_draw_tower(u64 now) {
  fill_rect(0, 0, TOP_W, TOP_H, C2D_Color32(0x99, 0xcc, 0xff, 255));
  float x = P.dive.m.x / 100.0f, y = P.dive.m.y / 100.0f;
  float ox = TOP_W / 2.0f - x, oy = TOP_H * .45f - y;
  ox = fminf(0, fmaxf(TOP_W - 720, ox));
  oy = fminf(0, fmaxf(TOP_H - 558, oy));
  ox = roundf(ox); oy = roundf(oy);
  for (int i = 0; i < 4; i++) {
    // poolClouds Class: clouds drift slowly across the sky.
    float cx = fmodf(now / 120.0f + i * 130, 850) - 80, cy = 20 + i * 22;
    char n[12]; snprintf(n, sizeof n, "pilvi%d", i % 5 + 1);
    const SceneArt *a = art(n);
    if (a) gfx_art_draw(a, ox + cx - a->rx, oy + cy - a->ry, 1, 1, 0xffffffff);
  }
  const SceneArt *bg = art("pelle_bg3"), *board = art("pomppulauta_4");
  if (bg) gfx_art_draw(bg, ox, oy, 1, 1, 0xffffffff);
  if (board) gfx_art_draw(board, ox + 489 - board->rx, oy + 131 - board->ry, 1, 1, 0xffffffff);
  Ent *e = world_ent(P.dive.eid);
  if (e) draw_diver(pool_look(e, e->look), P.dive.m.action, P.dive.m.direction, P.dive.m.frame, ox + x, oy + y, .5f, !P.dive.m.airborne);
  const char *help = P.dive.m.airborne ? "In the air: D-Pad for moves, B to somersault" : "Run: tap Left and Right in turn. A jumps";
  float w = text_width(help, 10) + 12;
  fill_round((TOP_W - w) / 2, TOP_H - 22, w, 17, 4, C2D_Color32(0, 0, 0, 170));
  text_draw(help, TOP_W / 2, TOP_H - 20, 10, C2D_Color32(255, 255, 255, 255), 1);
}

bool pool_camera(float *wx, float *wy, u64 now) {
  (void)now;
  if (P.wob.on && model_is("md_a") && (P.wob.p[0].eid == W.me || P.wob.p[1].eid == W.me)) {
    float middle = (P.wob.p[0].position + P.wob.p[1].position) / 2.0f;
    *wx = PAALU_ZERO[0] + middle * 16 + 40; *wy = PAALU_ZERO[1] - middle * 8 - 40;
    return true;
  }
  if (P.dive.on && model_is("pool_b") && P.dive.eid == W.me && P.dive.phase == 1 && P.dive.m.lower) {
    *wx = (P.dive.m.x / 100.0f - 216) * 2; *wy = (P.dive.m.y / 100.0f - 30) * 2 + 60;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- "Press A" in the Lido

typedef struct { const char *model; int x, y, radius; const char *label; int kind; const char *target; int tx, ty; } Spot;
enum { SPOT_AREA, SPOT_BOOTH, SPOT_DIVE, SPOT_POLE };
static const Spot SPOTS[] = {
  {"pool_a", 25, 9, 2, "Go to the Diving Deck", SPOT_AREA, "diving", 25, 9},
  {"pool_b", 9, 22, 2, "Go to The Pool", SPOT_AREA, "pool", 9, 22},
  {"pool_a", 17, 11, 2, "Use the changing booth", SPOT_BOOTH, NULL, 17, 11},
  {"pool_a", 17, 9, 2, "Use the changing booth", SPOT_BOOTH, NULL, 17, 9},
  {"md_a", 8, 1, 2, "Use the changing booth", SPOT_BOOTH, NULL, 8, 1},
  {"md_a", 9, 1, 2, "Use the changing booth", SPOT_BOOTH, NULL, 9, 1},
  {"pool_b", 26, 4, 3, "Join the diving queue", SPOT_DIVE, NULL, 26, 4},
  {"md_a", 21, 19, 2, "Join Wobble Squabble", SPOT_POLE, NULL, 21, 19},
  {"md_a", 21, 7, 2, "Join Wobble Squabble", SPOT_POLE, NULL, 21, 7},
};
static const Spot *spot;

bool pool_context(char *label, int cap, u64 now) {
  spot = NULL;
  Ent *me = world_ent(W.me);
  if (!pool_room() || !me || ent_moving(me, now) || pool_panel_active()) return false;
  int best = 99;
  for (int i = 0; i < (int)COUNT(SPOTS); i++) {
    const Spot *s = &SPOTS[i];
    if (!model_is(s->model)) continue;
    int dx = abs(me->x - s->x), dy = abs(me->y - s->y), d = dx > dy ? dx : dy;
    if (d > s->radius || d >= best || (d == 0 && s->kind != SPOT_AREA)) continue;
    if (s->kind == SPOT_DIVE && (queued(P.dive_queue, P.dive_queue_n, W.me) || (P.dive.on && P.dive.eid == W.me))) continue;
    if (s->kind == SPOT_POLE && queued(P.wobble_queue, P.wobble_queue_n, W.me)) continue;
    best = d; spot = s;
  }
  if (!spot) return false;
  snprintf(label, cap, "%s", TR(spot->label));
  return true;
}

void pool_context_run(u64 now) {
  if (!spot) return;
  switch (spot->kind) {
    case SPOT_AREA:
      // As in Kepler: walk to the signpost, then continue into the other Lido area.
      snprintf(P.area_target, sizeof P.area_target, "%s", spot->target);
      P.area_x = spot->tx; P.area_y = spot->ty; P.area_at = now;
      send_walk(spot->tx, spot->ty); scene_mark_destination(spot->tx, spot->ty, now);
      break;
    case SPOT_BOOTH: send_walk(spot->tx, spot->ty); scene_mark_destination(spot->tx, spot->ty, now); break;
    case SPOT_DIVE:
      if (pool_swimsuit(W.me) < 0) { ui_toast(TR("Change into a swimsuit at The Pool first."), now); break; }
      command("{\"op\":\"dive_queue\"}");
      break;
    case SPOT_POLE:
      if (pool_swimsuit(W.me) < 0) { ui_toast(TR("Use the changing booth before joining Wobble Squabble."), now); break; }
      command("{\"op\":\"wobble_join\"}");
      break;
  }
}

void pool_poll(u64 now) {
  if (!P.area_target[0]) return;
  Ent *me = world_ent(W.me);
  if (!me || !pool_room()) { P.area_target[0] = 0; return; }
  if (ent_moving(me, now)) return;
  if (me->x == P.area_x && me->y == P.area_y) {
    snprintf(P.arrive_from, sizeof P.arrive_from, "%s", model_is("pool_a") ? "pool" : "diving");
    char target[16]; snprintf(target, sizeof target, "%s", P.area_target);
    P.area_target[0] = 0;
    net_goto(target, "");
  } else if (me->next_x == 255 && now - P.area_at > 1500) P.area_target[0] = 0;  // the walk was given up
}

// ---------------------------------------------------------------- bottom-screen controls

enum { HUD_NONE, HUD_BOOTH, HUD_DIVE, HUD_VOTE, HUD_WOBBLE };
static int hud(void) {
  Swimmer *s = swimmer(W.me);
  if (s && s->booth) return HUD_BOOTH;
  if (P.wob.on && P.wob.phase == 1 && (P.wob.p[0].eid == W.me || P.wob.p[1].eid == W.me)) return HUD_WOBBLE;
  if (P.dive.on && P.dive.eid == W.me && P.dive.phase < 2 && !P.dive.m.lower) return HUD_DIVE;
  if (model_is("pool_b") && P.dive.on && P.dive.phase == 2 && P.dive.eid != W.me && !P.dive.voted) return HUD_VOTE;
  return HUD_NONE;
}
bool pool_panel_active(void) { return pool_room() && P.known && hud() != HUD_NONE; }

static void dive_move(int action) {
  char json[48]; snprintf(json, sizeof json, "{\"op\":\"dive_move\",\"action\":%d}", action); command(json);
}
static void wobble_move(int action) {
  char json[48]; snprintf(json, sizeof json, "{\"op\":\"wobble_move\",\"action\":%d}", action); command(json);
}
static void outfit(int color) {
  char json[48];
  if (color < 0) snprintf(json, sizeof json, "{\"op\":\"outfit\",\"remove\":true}");
  else snprintf(json, sizeof json, "{\"op\":\"outfit\",\"color\":%d}", color);
  command(json);
}
static int booth_pick = 0;

bool pool_input(u32 down, u32 held, u64 now) {
  (void)held; (void)now;
  switch (hud()) {
    case HUD_BOOTH:
      if (down & KEY_DLEFT) booth_pick = (booth_pick + 15) % 16;
      if (down & KEY_DRIGHT) booth_pick = (booth_pick + 1) % 16;
      if (down & KEY_DUP) booth_pick = (booth_pick + 8) % 16;
      if (down & KEY_DDOWN) booth_pick = (booth_pick + 8) % 16;
      if (down & KEY_A) outfit(booth_pick);
      if (down & KEY_X) outfit(-1);
      if (down & KEY_B) command("{\"op\":\"leave\"}");
      return true;
    case HUD_DIVE:
      // Original keys: A and D run in turn, Space jumps; in the air W/E/Z/X pose and S somersaults.
      if (!P.dive.m.airborne) {
        if (down & KEY_DLEFT) dive_move(1);
        if (down & KEY_DRIGHT) dive_move(2);
        if (down & KEY_A) dive_move(8);
      } else {
        if (down & KEY_DUP) dive_move(3);
        if (down & KEY_DRIGHT) dive_move(4);
        if (down & KEY_DDOWN) dive_move(5);
        if (down & KEY_DLEFT) dive_move(6);
        if (down & KEY_B) dive_move(7);
      }
      return true;
    case HUD_WOBBLE:
      // Q/E balance, A/D push, N/M move, Space stabilises once.
      if (down & KEY_L) wobble_move(1);
      if (down & KEY_R) wobble_move(2);
      if (down & KEY_Y) wobble_move(3);
      if (down & KEY_A) wobble_move(4);
      if (down & KEY_DLEFT) wobble_move(5);
      if (down & KEY_DRIGHT) wobble_move(6);
      if (down & KEY_X) wobble_move(7);
      return true;
    default:
      return false;
  }
}

static bool button(float x, float y, float w, const char *label) { return sk_button(x, y, w, label, true); }

void pool_draw_panel(u64 now) {
  int mode = hud();
  const char *title = mode == HUD_BOOTH ? "Changing booth" : mode == HUD_DIVE ? "Diving tower" : mode == HUD_VOTE ? "Rate the dive" : "Wobble Squabble";
  sk_status_window(WIN_X, WIN_Y, WIN_W, WIN_H, title);
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  float x = IN_X + 6, y = IN_Y + 6;
  if (mode == HUD_BOOTH) {
    sk_paragraph(TR("Choose a swimsuit (D-Pad and A). Your usual clothes stay saved."), x, y, IN_W - 12, 10, SK_TEXT, 2);
    for (int c = 0; c < 16; c++) {
      float bx = x + (c % 8) * 37, by = y + 30 + (c / 8) * 37;
      if (c == booth_pick) fill_round(bx - 2, by - 2, 35, 35, 5, sk_rgb(SK_ACCENT));
      fill_round(bx, by, 31, 31, 4, sk_rgb(0x222222));
      fill_round(bx + 2, by + 2, 27, 27, 3, sk_rgb(SHIRT[c]));
      if (sk_hit(bx, by, 31, 31) && T.released) { booth_pick = c; outfit(c); }
    }
    if (button(x, y + 110, 150, "Clothes back on (X)")) outfit(-1);
    if (button(x + 160, y + 110, 120, "Leave (B)")) command("{\"op\":\"leave\"}");
  } else if (mode == HUD_DIVE) {
    if (!P.dive.m.airborne) {
      sk_paragraph(TR("Run along the board: Left and Right in turn. Jump with A before the end."), x, y, IN_W - 12, 10, SK_TEXT, 2);
      if (button(x, y + 40, 90, "Left")) dive_move(1);
      if (button(x + 96, y + 40, 90, "Right")) dive_move(2);
      if (sk_button_big(x + 192, y + 36, 98, TR("Jump (A)"), true)) dive_move(8);
    } else {
      sk_paragraph(TR("Combine moves in the air. B turns a somersault."), x, y, IN_W - 12, 10, SK_TEXT, 2);
      static const char *names[] = {"Up", "Right", "Down", "Left"};
      for (int i = 0; i < 4; i++) if (button(x + (i % 2) * 140, y + 34 + (i / 2) * 28, 130, TR(names[i]))) dive_move(3 + i);
      if (sk_button_big(x + 70, y + 94, 150, TR("Somersault (B)"), true)) dive_move(7);
    }
  } else if (mode == HUD_VOTE) {
    sk_paragraph(TR("How was the dive? The room's average is the score."), x, y, IN_W - 12, 10, SK_TEXT, 2);
    for (int s = 1; s <= 10; s++) {
      char n[4]; snprintf(n, sizeof n, "%d", s);
      if (button(x + ((s - 1) % 5) * 58, y + 34 + ((s - 1) / 5) * 28, 52, n)) {
        char json[40]; snprintf(json, sizeof json, "{\"op\":\"vote\",\"score\":%d}", s); command(json);
        P.dive.voted = true;
      }
    }
  } else if (mode == HUD_WOBBLE) {
    const Wobbler *me = P.wob.p[0].eid == W.me ? &P.wob.p[0] : &P.wob.p[1];
    s64 t = server_now(now);
    char status[64];
    if (t < P.wob.starts) snprintf(status, sizeof status, TR("Get ready… %d"), (int)((P.wob.starts - t + 999) / 1000));
    else snprintf(status, sizeof status, TR("%d s left"), (int)((P.wob.ends - t + 999) / 1000 > 0 ? (P.wob.ends - t + 999) / 1000 : 0));
    sk_text(status, x, y, 11, SK_TEXT, 0, IN_W - 12);
    // Balance needle: falling off at ±100.
    float mx = x + 10, mw = IN_W - 32, my = y + 22;
    fill_round(mx, my, mw, 12, 4, sk_rgb(0x9fc9a0));
    fill_rect(mx, my, mw * .15f, 12, sk_rgb(0xd9706a));
    fill_rect(mx + mw * .85f, my, mw * .15f, 12, sk_rgb(0xd9706a));
    float needle = mx + mw / 2 + (me->balance < -100 ? -100 : me->balance > 100 ? 100 : me->balance) * mw / 200;
    fill_rect(needle - 2, my - 3, 4, 18, sk_rgb(0x111111));
    if (button(x, y + 46, 140, "Lean left (L)")) wobble_move(1);
    if (button(x + 148, y + 46, 140, "Lean right (R)")) wobble_move(2);
    if (button(x, y + 74, 140, "Push left (Y)")) wobble_move(3);
    if (button(x + 148, y + 74, 140, "Push right (A)")) wobble_move(4);
    if (button(x, y + 102, 140, "Step back (Left)")) wobble_move(5);
    if (button(x + 148, y + 102, 140, "Step on (Right)")) wobble_move(6);
    if (!me->rebalanced && sk_button_big(x + 70, y + 128, 150, TR("Steady (X)"), true)) wobble_move(7);
  }
}
