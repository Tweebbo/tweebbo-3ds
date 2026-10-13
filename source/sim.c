#include "sim.h"
#include "assets.h"
#include "catalog.h"
#include "path.h"
#include "proto.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define SIM_ENTS 16
#define SIM_ITEMS 128
#define PATH_CAP 160
#define TICK_MS 500
#define HANDOUT_TICKS 600
#define DRINK_TICKS 180
#define BOT_COUNT 6

typedef struct {
  bool used, bot, leaving;
  u32 id, guest;
  char name[24];
  u64 look;
  u8 x, y, dir, flags;
  u16 z;
  u8 path[PATH_CAP][2];
  int plen, ppos;
  int wave_ticks, active_ticks, drink_ticks;
  bool dirty;
  u8 drink, pending_drink;
  u32 pending_item;
  u64 next_think, reply_at, stop_dance_at;
  char reply[64];
} SEnt;

typedef struct { bool used; u32 id; u16 def; u8 x, y, rot, state; u16 z; } SItem;

static struct {
  const RoomModel *model;
  u8 w, h;
  char rows[64 * 64];
  SItem items[SIM_ITEMS];
  SEnt ents[SIM_ENTS];
  u32 next_eid, me, next_guest;
  u64 next_tick, next_spawn;
  int spawned;
  u32 credits;
} S;

// ---------------------------------------------------------------- helpers

static bool tile(int x, int y) { return x >= 0 && y >= 0 && x < S.w && y < S.h && S.rows[y * S.w + x] != 'x'; }

static SEnt *ent(u32 id) {
  for (int i = 0; i < SIM_ENTS; i++) if (S.ents[i].used && S.ents[i].id == id) return &S.ents[i];
  return NULL;
}

static SItem *item(u32 id) {
  for (int i = 0; i < SIM_ITEMS; i++) if (S.items[i].used && S.items[i].id == id) return &S.items[i];
  return NULL;
}

static bool covers(const SItem *it, int x, int y) {
  int w, h;
  def_size(it->def, it->rot, &w, &h);
  return x >= it->x && y >= it->y && x < it->x + w && y < it->y + h;
}

static SItem *item_at(int x, int y) {
  SItem *best = NULL;
  if (!tile(x, y)) return NULL;
  for (int i = 0; i < SIM_ITEMS; i++) {
    SItem *it = &S.items[i];
    const FurniDef *d = it->used ? def_get(it->def) : NULL;
    if (!d || d->kind == KIND_WALL || !covers(it, x, y)) continue;
    if (!best || it->z > best->z || (it->z == best->z && d->kind != KIND_FLOOR)) best = it;
  }
  return best;
}

static bool can_walk(int x, int y, bool goal) {
  if (!tile(x, y)) return false;
  SItem *it = item_at(x, y);
  if (!it) return true;
  const FurniDef *d = def_get(it->def);
  if (!d) return false;
  if (it->state > 0 && (d->flags & DEF_GATE)) return true;
  if (d->flags & (DEF_LAY | DEF_TELEPORTER)) return goal;
  if (d->kind == KIND_FLOOR) return true;
  if (d->kind == KIND_SIT) return goal;
  return false;
}

static bool occupied(int x, int y, u32 except) {
  for (int i = 0; i < SIM_ENTS; i++) {
    SEnt *e = &S.ents[i];
    if (e->used && e->id != except && e->x == x && e->y == y) return true;
  }
  return false;
}

static bool walk_cb(void *ctx, int x, int y, bool goal) {
  return can_walk(x, y, goal) && !occupied(x, y, *(u32 *)ctx);
}

static void send(const Writer *w) { sim_deliver(w->buf, w->len); }

static void w_ent(Writer *w, const SEnt *e) {
  w_var(w, e->id); w_str(w, e->name);
  w_u8(w, e->x); w_u8(w, e->y); w_u8(w, e->dir); w_u8(w, e->flags);
  w_var(w, e->look); w_u16(w, e->z); w_var(w, e->guest);
}

static void w_item(Writer *w, const SItem *it) {
  w_var(w, it->id); w_u16(w, it->def);
  w_u8(w, it->x); w_u8(w, it->y); w_u8(w, it->rot); w_u8(w, it->state);
  w_var(w, 0); w_u16(w, it->z);
}

static void notice(const char *text) {
  Writer w; w_begin(&w, S_NOTICE); w_str(&w, text); send(&w);
}

static int count_ents(void) {
  int n = 0;
  for (int i = 0; i < SIM_ENTS; i++) n += S.ents[i].used;
  return n;
}

static void online(void) {
  Writer w; w_begin(&w, S_ONLINE); w_var(&w, (u64)count_ents()); send(&w);
}

static void carry(const SEnt *e) {
  Writer w; w_begin(&w, S_CARRY); w_var(&w, e->id); w_u8(&w, e->drink); w_var(&w, (u64)e->drink_ticks * TICK_MS); send(&w);
}

// ---------------------------------------------------------------- actions

static void walk_to(SEnt *e, int x, int y) {
  SItem *bed = item_at(x, y);
  const FurniDef *bd = bed ? def_get(bed->def) : NULL;
  if (bd && (bd->flags & DEF_LAY)) { if (bed->rot % 2 == 0) y = bed->y; else x = bed->x; }
  if (!tile(x, y) || (e->x == x && e->y == y) || occupied(x, y, e->id)) return;
  static u8 path[PATH_CAP][2];
  int n = path_find(S.w, S.h, e->x, e->y, x, y, walk_cb, &e->id, path, PATH_CAP);
  if (n < 0) return;
  memcpy(e->path, path, sizeof(u8[2]) * (size_t)n);
  e->plen = n; e->ppos = 0;
  if (e->flags & (FLAG_SIT | FLAG_LAY)) { e->flags &= ~(FLAG_SIT | FLAG_LAY); e->dirty = true; }
}

static void say(SEnt *e, const char *text) {
  char clean[101];
  int n = 0;
  for (const char *p = text; *p && n < 100; p++) if ((u8)*p >= 32) clean[n++] = *p;
  clean[n] = 0;
  char *start = clean;
  while (*start == ' ') start++;
  for (int i = (int)strlen(start) - 1; i >= 0 && start[i] == ' '; i--) start[i] = 0;
  if (!*start) return;
  Writer w; w_begin(&w, S_CHAT); w_var(&w, e->id); w_str(&w, start); send(&w);
}

static void gesture(SEnt *e, int kind) {
  if (e->flags & FLAG_LAY) return;
  switch (kind) {
    case GESTURE_WAVE: e->flags |= FLAG_WAVE; e->wave_ticks = 4; break;
    case GESTURE_DANCE: e->flags ^= FLAG_DANCE; e->flags &= ~FLAG_SIT; break;
    case GESTURE_SIT:
      if (e->ppos < e->plen) return;
      e->flags ^= FLAG_SIT; e->flags &= ~FLAG_DANCE; e->dir &= ~1;
      break;
    default: return;
  }
  e->dirty = true;
}

static bool beside(const SEnt *e, const SItem *it) {
  int w, h;
  def_size(it->def, it->rot, &w, &h);
  for (int y = it->y; y < it->y + h; y++)
    for (int x = it->x; x < it->x + w; x++)
      if ((e->x > x ? e->x - x : x - e->x) + (e->y > y ? e->y - y : y - e->y) == 1) return true;
  return false;
}

static void serve(SEnt *e, SItem *it, u8 drink) {
  e->drink = drink; e->drink_ticks = DRINK_TICKS; e->pending_item = 0;
  // Face the dispenser when receiving the drink.
  e->dir = dir_of(it->x - e->x, it->y - e->y);
  e->dirty = true;
  carry(e);
}

static void request_drink(SEnt *e, u32 id, u8 drink) {
  if (!id && !drink) { e->drink = 0; e->drink_ticks = 0; e->pending_item = 0; carry(e); return; }
  SItem *it = item(id);
  const FurniDef *d = it ? def_get(it->def) : NULL;
  if (!d || !d->drink) return;
  drink = d->drink;
  e->pending_item = 0;
  if (beside(e, it)) { serve(e, it, drink); return; }
  int w, h, n = 0;
  u8 cand[32][3];
  def_size(it->def, it->rot, &w, &h);
  static const s8 N4[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
  // Free tiles next to the dispenser, nearest first.
  for (int y = it->y; y < it->y + h; y++)
    for (int x = it->x; x < it->x + w; x++)
      for (int k = 0; k < 4 && n < 32; k++) {
        int nx = x + N4[k][0], ny = y + N4[k][1];
        if (!can_walk(nx, ny, false) || occupied(nx, ny, e->id)) continue;
        int dist = (e->x > nx ? e->x - nx : nx - e->x) + (e->y > ny ? e->y - ny : ny - e->y);
        cand[n][0] = (u8)nx; cand[n][1] = (u8)ny; cand[n][2] = (u8)(dist > 255 ? 255 : dist); n++;
      }
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && cand[j][2] < cand[j - 1][2]; j--) {
      u8 t[3]; memcpy(t, cand[j], 3); memcpy(cand[j], cand[j - 1], 3); memcpy(cand[j - 1], t, 3);
    }
  for (int i = 0; i < n; i++) {
    walk_to(e, cand[i][0], cand[i][1]);
    if (e->ppos < e->plen) { e->pending_item = id; e->pending_drink = drink; return; }
  }
  if (!e->bot) notice("Leave a free tile beside the dispenser to get a drink.");
}

static bool valid_name(const char *n) {
  int len = (int)strlen(n);
  if (len < 3 || len > 20) return false;
  for (const char *p = n; *p; p++) if (!isalnum((u8)*p) && *p != '_' && *p != '-') return false;
  return true;
}

static bool valid_look(u64 look) {
  int hair = (int)(((look >> 2) & 7) | (((look >> 19) & 7) << 3));
  int top = (int)(((look >> 12) & 3) | (((look >> 22) & 31) << 2));
  return (look >> 32) <= (u64)wardrobe_faces_count && hair < wardrobe_hair_count && top < wardrobe_tops_count &&
         (int)((look >> 27) & 7) < wardrobe_bottoms_count && (int)((look >> 30) & 3) < wardrobe_shoes_count;
}

// ---------------------------------------------------------------- bots

static const char *ANIMALS[] = {"Penguin", "Fox", "Cat", "Panda", "Koala", "Owl", "Otter", "Tiger", "Hedgehog",
                                "Lynx", "Seal", "Llama", "Raccoon", "Badger", "Dolphin", "Falcon", "Octopus", "Axolotl"};
static const char *ADJECTIVES[] = {"Swift", "Happy", "Wise", "Wild", "Calm", "Brave", "Curious", "Golden", "Shy",
                                   "Bold", "Night", "Cosmic", "Chill", "Turbo"};
static const char *LINES[] = {
  "Hey everyone!", "This lobby is so cozy", "Anyone want a coffee?", "brb", "lol",
  "Who else is on a 3DS?", "Nice outfit!", "I love these sofas", "Hello from the browser!",
  "Let's dance!", "Has anyone seen the park?", "The cola machine works!", "gg",
  "First time here, hi!", "Chilling...", "What's up?",
};
static const char *GREETINGS[] = {"Hi!", "Hey there!", "Hello :)", "Welcome!", "Yo!", "Hiya!"};

void random_name(char *out, int cap) {
  snprintf(out, (size_t)cap, "%s%s%d", ANIMALS[rnd_range(0, COUNT(ANIMALS))],
           ADJECTIVES[rnd_range(0, COUNT(ADJECTIVES))], rnd_range(10, 1000));
}

u64 random_look(void) {
  return (u64)rnd_range(0, 4) | (u64)rnd_range(0, 7) << 2 | (u64)rnd_range(0, 8) << 5 |
         (u64)rnd_range(0, 16) << 8 | (u64)rnd_range(0, 4) << 12 | (u64)rnd_range(0, 8) << 14 |
         (u64)rnd_range(0, 4) << 17;
}

/** Tiles next to the door: if someone stops there, nobody else can enter. */
static bool near_door(int x, int y) {
  int dx = x - S.model->door_x, dy = y - S.model->door_y;
  return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
}

/** Walks in to the arrival tile or, if taken, the nearest free one. */
static void enter(SEnt *e) {
  int tx = S.model->target_x, ty = S.model->target_y;
  if (tx == 255) return;
  for (int r = 0; r < 4; r++)
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++) {
        if ((dx < 0 ? -dx : dx) != r && (dy < 0 ? -dy : dy) != r) continue;
        int x = tx + dx, y = ty + dy;
        if (!can_walk(x, y, false) || occupied(x, y, e->id) || near_door(x, y)) continue;
        walk_to(e, x, y);
        if (e->ppos < e->plen) return;
      }
}

static SEnt *add_ent(const char *name, u64 look, bool bot) {
  SEnt *e = NULL;
  for (int i = 0; i < SIM_ENTS && !e; i++) if (!S.ents[i].used) e = &S.ents[i];
  if (!e) return NULL;
  memset(e, 0, sizeof *e);
  e->used = true; e->bot = bot;
  e->id = S.next_eid++;
  e->guest = S.next_guest++;
  snprintf(e->name, sizeof e->name, "%s", name);
  e->look = look;
  e->x = S.model->door_x; e->y = S.model->door_y; e->dir = S.model->dir;
  Writer w; w_begin(&w, S_ENTITY_ADD); w_ent(&w, e); send(&w);
  enter(e);
  online();
  return e;
}

static void remove_ent(SEnt *e) {
  Writer w; w_begin(&w, S_ENTITY_REMOVE); w_var(&w, e->id); send(&w);
  e->used = false;
  online();
}

static void random_tile(int *x, int *y) {
  for (int tries = 0; tries < 200; tries++) {
    int tx = rnd_range(0, S.w), ty = rnd_range(0, S.h);
    if (can_walk(tx, ty, false) && !occupied(tx, ty, 0) && !near_door(tx, ty)) { *x = tx; *y = ty; return; }
  }
  *x = *y = -1;
}

static bool random_seat(int *x, int *y) {
  int seen = 0;
  *x = *y = -1;
  for (int i = 0; i < SIM_ITEMS; i++) {
    SItem *it = &S.items[i];
    const FurniDef *d = it->used ? def_get(it->def) : NULL;
    if (!d || d->kind != KIND_SIT) continue;
    int w, h;
    def_size(it->def, it->rot, &w, &h);
    for (int ty = it->y; ty < it->y + h; ty++)
      for (int tx = it->x; tx < it->x + w; tx++)
        if (!occupied(tx, ty, 0) && rnd_range(0, ++seen) == 0) { *x = tx; *y = ty; }
  }
  return *x >= 0;
}

static SItem *random_dispenser(void) {
  SItem *pick = NULL;
  int seen = 0;
  for (int i = 0; i < SIM_ITEMS; i++) {
    const FurniDef *d = S.items[i].used ? def_get(S.items[i].def) : NULL;
    if (d && d->drink && rnd_range(0, ++seen) == 0) pick = &S.items[i];
  }
  return pick;
}

static void think(SEnt *e, u64 now) {
  if (e->reply[0] && now >= e->reply_at) { say(e, e->reply); e->reply[0] = 0; }
  if (e->stop_dance_at && now >= e->stop_dance_at) {
    e->stop_dance_at = 0;
    if (e->flags & FLAG_DANCE) gesture(e, GESTURE_DANCE);
  }
  if (e->ppos < e->plen || e->pending_item || now < e->next_think) return;
  if (e->leaving) { remove_ent(e); S.next_spawn = now + (u64)rnd_range(8000, 20000); return; }
  e->next_think = now + (u64)rnd_range(2500, 9000);
  int r = rnd_range(0, 100), x, y;
  if (r < 40) { random_tile(&x, &y); if (x >= 0) walk_to(e, x, y); }
  else if (r < 58) { if (random_seat(&x, &y)) walk_to(e, x, y); }
  else if (r < 65) gesture(e, GESTURE_WAVE);
  else if (r < 71 && !(e->flags & FLAG_SIT)) {
    gesture(e, GESTURE_DANCE);
    if (e->flags & FLAG_DANCE) e->stop_dance_at = now + (u64)rnd_range(6000, 15000);
  }
  else if (r < 86) say(e, LINES[rnd_range(0, COUNT(LINES))]);
  else if (r < 93 && !e->drink) { SItem *it = random_dispenser(); if (it) request_drink(e, it->id, 0); }
  else if (r < 95 && count_ents() > 3) {
    // Leaves through the door; another visitor will arrive later.
    e->leaving = true;
    if (S.model->target_x != 255) walk_to(e, S.model->target_x, S.model->target_y);
  }
}

static void bots_answer(const char *text, u64 now) {
  char low[128];
  int n = 0;
  for (const char *p = text; *p && n < 127; p++) low[n++] = (char)tolower((u8)*p);
  low[n] = 0;
  if (!strstr(low, "hi") && !strstr(low, "hello") && !strstr(low, "hey")) return;
  int answered = 0;
  for (int i = 0; i < SIM_ENTS && answered < 2; i++) {
    SEnt *e = &S.ents[i];
    if (!e->used || !e->bot || e->reply[0] || rnd_range(0, 3) == 0) continue;
    snprintf(e->reply, sizeof e->reply, "%s", GREETINGS[rnd_range(0, COUNT(GREETINGS))]);
    e->reply_at = now + (u64)rnd_range(700, 2500);
    gesture(e, GESTURE_WAVE);
    answered++;
  }
}

// ---------------------------------------------------------------- tick

static void tick(u64 now) {
  for (int i = 0; i < SIM_ENTS; i++) {
    SEnt *e = &S.ents[i];
    if (!e->used) continue;
    if (e->ppos < e->plen) {
      int nx = e->path[e->ppos][0], ny = e->path[e->ppos][1];
      bool last = e->ppos == e->plen - 1;
      if (can_walk(nx, ny, last) && !occupied(nx, ny, e->id)) {
        e->ppos++;
        e->dir = dir_of(nx - e->x, ny - e->y);
        e->x = (u8)nx; e->y = (u8)ny;
        SItem *seat = last ? item_at(nx, ny) : NULL;
        const FurniDef *d = seat ? def_get(seat->def) : NULL;
        if (d && (d->kind == KIND_SIT || (d->flags & DEF_LAY))) {
          e->flags = (u8)((e->flags & ~(FLAG_SIT | FLAG_LAY | FLAG_DANCE | FLAG_WAVE)) | (d->kind == KIND_SIT ? FLAG_SIT : FLAG_LAY));
          e->dir = (u8)(seat->rot * 2);
        }
      } else {
        e->plen = e->ppos = 0;
      }
      e->dirty = true;
    }
    if (e->wave_ticks > 0 && --e->wave_ticks == 0) { e->flags &= ~FLAG_WAVE; e->dirty = true; }
    if (e->ppos >= e->plen && e->pending_item) {
      SItem *it = item(e->pending_item);
      if (it && beside(e, it)) serve(e, it, e->pending_drink);
      e->pending_item = 0;
    }
    if (e->drink_ticks > 0 && --e->drink_ticks == 0) { e->drink = 0; carry(e); }
    if (!e->bot && ++e->active_ticks >= HANDOUT_TICKS) {
      e->active_ticks = 0;
      S.credits += 10;
      Writer w; w_begin(&w, S_CREDITS); w_var(&w, S.credits); send(&w);
      notice("+10 credits for hanging out here.");
      profile_save(e->name, e->look, S.credits);
    }
  }
  int dirty = 0;
  for (int i = 0; i < SIM_ENTS; i++) dirty += S.ents[i].used && S.ents[i].dirty;
  if (dirty) {
    Writer w; w_begin(&w, S_DELTAS); w_var(&w, (u64)dirty);
    for (int i = 0; i < SIM_ENTS; i++) {
      SEnt *e = &S.ents[i];
      if (!e->used || !e->dirty) continue;
      e->dirty = false;
      bool more = e->ppos < e->plen;
      w_var(&w, e->id); w_u8(&w, e->x); w_u8(&w, e->y); w_u8(&w, e->dir); w_u8(&w, e->flags);
      w_u8(&w, more ? e->path[e->ppos][0] : 255); w_u8(&w, more ? e->path[e->ppos][1] : 255);
      w_u16(&w, e->z);
    }
    send(&w);
  }
  (void)now;
}

// ---------------------------------------------------------------- API

void sim_start(const char *room_key, u64 now) {
  memset(&S, 0, sizeof S);
  S.model = &room_models[0];
  for (int i = 0; i < room_model_count; i++) if (!strcmp(room_models[i].key, room_key)) S.model = &room_models[i];
  const char *p = S.model->rows;
  int y = 0, w = 0;
  memset(S.rows, 'x', sizeof S.rows);
  // First pass: dimensions; second: copy the rows.
  for (const char *q = p; *q; y++) {
    int x = 0;
    while (q[x] && q[x] != '\n') x++;
    if (x > w) w = x;
    q += x + (q[x] == '\n');
  }
  S.w = (u8)w; S.h = (u8)y;
  for (int ry = 0; ry < S.h && *p; ry++) {
    for (int x = 0; *p && *p != '\n'; x++, p++) S.rows[ry * S.w + x] = *p;
    if (*p == '\n') p++;
  }
  for (int i = 0; i < S.model->item_count && i < SIM_ITEMS; i++) {
    const RoomItem *src = &S.model->items[i];
    S.items[i] = (SItem){true, src->id, src->def, src->x, src->y, src->rot, 0, src->z};
  }
  S.next_eid = 1;
  S.next_guest = 1000;
  S.next_tick = now + TICK_MS;
  S.next_spawn = now + 1500;
}

static void join(const char *token, u64 now) {
  char name[24];
  u64 look;
  profile_load(name, sizeof name, &look, &S.credits);
  if (!valid_name(name)) random_name(name, sizeof name);
  if (!valid_look(look)) look = random_look();
  profile_save(name, look, S.credits);
  SEnt *e = add_ent(name, look, false);
  if (!e) return;
  S.me = e->id;
  Writer w;
  w_begin(&w, S_WELCOME);
  w_var(&w, e->id); w_str(&w, name); w_str(&w, token[0] ? token : "offline"); w_var(&w, S.credits); w_var(&w, e->guest);
  send(&w);
  // Room snapshot, in the same format as Room::snapshot.
  static char rows[64 * 65 + 1];
  int n = 0;
  for (int y = 0; y < S.h; y++) {
    for (int x = 0; x < S.w; x++) rows[n++] = S.rows[y * S.w + x];
    if (y + 1 < S.h) rows[n++] = '\n';
  }
  rows[n] = 0;
  // The snapshot may exceed the normal message buffer.
  static u8 big[16384];
  static Writer snapshot;
  Writer *room = &snapshot;
  w_begin_into(room, S_ROOM, big, sizeof big);
  w_str(room, "lobby"); w_str(room, "Tweebbo Lobby"); w_str(room, S.model->key);
  w_u8(room, S.w); w_u8(room, S.h); w_str(room, rows);
  w_u8(room, S.model->door_x); w_u8(room, S.model->door_y);
  w_var(room, 0); w_str(room, "");
  int items = 0;
  for (int i = 0; i < SIM_ITEMS; i++) items += S.items[i].used;
  w_var(room, (u64)items);
  for (int i = 0; i < SIM_ITEMS; i++) if (S.items[i].used) w_item(room, &S.items[i]);
  w_var(room, (u64)count_ents());
  for (int i = 0; i < SIM_ENTS; i++) if (S.ents[i].used) w_ent(room, &S.ents[i]);
  send(room);
  online();
  (void)now;
}

void sim_receive(const u8 *data, int len) {
  Reader r;
  r_init(&r, data, len);
  u64 now = now_ms();
  u8 kind = r_u8(&r);
  char text[128];
  if (kind == C_HELLO) {
    r_u16(&r);
    r_str(&r, text, sizeof text);
    if (!S.me) join(text, now);
    return;
  }
  SEnt *e = ent(S.me);
  if (!e) return;
  switch (kind) {
    case C_WALK: { int x = r_u8(&r), y = r_u8(&r); if (!r.err) { e->pending_item = 0; walk_to(e, x, y); } break; }
    case C_CHAT: r_str(&r, text, sizeof text); if (!r.err) { say(e, text); bots_answer(text, now); } break;
    case C_GESTURE: { int g = r_u8(&r); if (!r.err) gesture(e, g); break; }
    case C_SET_NAME:
      r_str(&r, text, sizeof text);
      if (r.err) break;
      if (!valid_name(text)) { notice("Names need 3-20 letters, numbers, _ or -."); break; }
      snprintf(e->name, sizeof e->name, "%s", text);
      { Writer w; w_begin(&w, S_ENTITY_NAME); w_var(&w, e->id); w_str(&w, e->name); send(&w); }
      profile_save(e->name, e->look, S.credits);
      break;
    case C_SET_LOOK: {
      u64 look = r_var(&r);
      if (r.err || !valid_look(look)) break;
      e->look = look;
      Writer w; w_begin(&w, S_ENTITY_LOOK); w_var(&w, e->id); w_var(&w, look); send(&w);
      profile_save(e->name, e->look, S.credits);
      break;
    }
    case C_USE: {
      SItem *it = item((u32)r_var(&r));
      const FurniDef *d = it ? def_get(it->def) : NULL;
      if (!d) break;
      if (d->drink) { request_drink(e, it->id, 0); break; }
      if (d->kind == KIND_TOGGLE) {
        it->state = it->state ? 0 : 1;
        Writer w; w_begin(&w, S_ITEM_UPDATE); w_item(&w, it); send(&w);
      }
      break;
    }
    case C_DRINK: { u32 id = (u32)r_var(&r); u8 drink = r_u8(&r); if (!r.err) request_drink(e, id, drink); break; }
    default:
      notice("Not available offline yet.");
      break;
  }
}

void sim_update(u64 now) {
  // After a long pause (system keyboard, HOME Menu) ticks do not pile up.
  if (now > S.next_tick + 2000) S.next_tick = now;
  while (now >= S.next_tick) {
    for (int i = 0; i < SIM_ENTS; i++) if (S.ents[i].used && S.ents[i].bot) think(&S.ents[i], S.next_tick);
    tick(S.next_tick);
    S.next_tick += TICK_MS;
  }
  int bots = 0;
  for (int i = 0; i < SIM_ENTS; i++) bots += S.ents[i].used && S.ents[i].bot;
  if (S.me && bots < BOT_COUNT && now >= S.next_spawn && !occupied(S.model->door_x, S.model->door_y, 0)) {
    char name[24];
    random_name(name, sizeof name);
    SEnt *b = add_ent(name, random_look(), true);
    if (b) b->next_think = now + (u64)rnd_range(2000, 5000);
    S.next_spawn = now + (u64)rnd_range(1500, 6000);
  }
}
