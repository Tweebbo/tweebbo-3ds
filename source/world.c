#include "world.h"
#include "lookup_cache.h"
#include "catalog.h"
#include "proto.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

World W;
static LookupCache entity_lookup;

bool world_tile(int x, int y) {
  return x >= 0 && y >= 0 && x < W.w && y < W.h && W.rows[y * W.w + x] != 'x';
}

Ent *world_ent(u32 id) {
  int cached=lookup_cache_get(&entity_lookup,id);
  if(cached>=0&&cached<MAX_ENTS&&W.ents[cached].used&&W.ents[cached].id==id)return &W.ents[cached];
  for (int i = 0; i < MAX_ENTS; i++) if (W.ents[i].used && W.ents[i].id == id) {lookup_cache_put(&entity_lookup,id,(unsigned)i);return &W.ents[i];}
  return NULL;
}

const char *public_slug(const char *slug) {
  // System rooms keep internal ids on the server; their public names follow the room model.
  if (W.owner == 0 && !strcmp(slug, W.slug)) {
    if (!strcmp(W.model, "newbie_lobby")) return "lobby";
    if (!strcmp(W.model, "park_a")) return "park";
    if (!strcmp(W.model, "pool_a")) return "pool";
  }
  return slug;
}

bool public_model(const char *model) {
  return !strcmp(model, "newbie_lobby") || !strcmp(model, "park_a") || !strcmp(model, "pool_a") || !strcmp(model, "casino");
}

bool world_can_decorate(void) { return W.owner && (W.owner == W.guest_id || W.room_rights) && !public_model(W.model); }
bool world_can_arrange(const Item *it) { return world_can_manage(it) || (W.room_rights && world_can_decorate()); }
bool world_can_manage(const Item *it) { return it->owner == W.guest_id || W.owner == W.guest_id; }

static void inv_add(u32 id, u16 def) {
  for (int i = 0; i < W.inv_n; i++) if (W.inv[i].id == id) { W.inv[i].def = def; W.inv_rev++; return; }
  if (W.inv_n < MAX_INV) W.inv[W.inv_n++] = (InvItem){id, def};
  W.inv_rev++;
}

static void inv_remove(u32 id) {
  for (int i = 0; i < W.inv_n; i++)
    if (W.inv[i].id == id) { memmove(&W.inv[i], &W.inv[i + 1], sizeof(InvItem) * (size_t)(W.inv_n - i - 1)); W.inv_n--; break; }
  W.inv_rev++;
}

static void set_json(char **dst, Reader *r) {
  int n = (int)r_var(r);
  if (r->err || n < 0 || r->p + n > r->len) { r->err = true; return; }
  char *s = malloc((size_t)n + 1);
  if (!s) return;
  memcpy(s, r->b + r->p, (size_t)n);
  s[n] = 0;
  r->p += n;
  free(*dst);
  *dst = s;
  W.pets_rev++;
}

Item *world_item(u32 id) {
  for (int i = 0; i < MAX_ITEMS; i++) if (W.items[i].used && W.items[i].id == id) return &W.items[i];
  return NULL;
}

Item *world_item_at(int x, int y) {
  Item *best = NULL;
  for (int i = 0; i < MAX_ITEMS; i++) {
    Item *it = &W.items[i];
    if (!it->used) continue;
    const FurniDef *d = def_get(it->def);
    if (d && d->kind == KIND_WALL) continue;
    int w, h;
    def_size(it->def, it->rot, &w, &h);
    if (x < it->x || y < it->y || x >= it->x + w || y >= it->y + h) continue;
    // Same as State.itemAt: the highest one; at equal height, non-rugs win.
    if (!best || it->z > best->z || (it->z == best->z && d && d->kind != KIND_FLOOR)) best = it;
  }
  return best;
}

static float progress(u64 t0, u64 now) {
  if (now <= t0) return 0;
  float t = (float)(now - t0) / MOVE_MS;
  return t > 1 ? 1 : t;
}

void ent_pos(const Ent *e, u64 now, float *x, float *y) {
  float t = progress(e->t0, now);
  // The door is the threshold in the wall, not the center of a tile.
  float shift = world_tile(W.door_x, W.door_y) ? -.5f : .5f;
  float from = e->fx + (e->fx == W.door_x && e->fy == W.door_y ? shift : 0);
  float to = e->x + (e->x == W.door_x && e->y == W.door_y ? shift : 0);
  *x = from + (to - from) * t;
  *y = e->fy + (e->y - e->fy) * t;
}

float ent_height(const Ent *e, u64 now) {
  return e->fz + (e->z - e->fz) * progress(e->t0, now);
}

bool ent_moving(const Ent *e, u64 now) {
  return now < e->t0 + MOVE_MS && (e->fx != e->x || e->fy != e->y);
}

static void read_item(Reader *r, Item *it) {
  *it = (Item){.used = true};
  it->id = (u32)r_var(r);
  it->def = r_u16(r);
  it->x = r_u8(r); it->y = r_u8(r); it->rot = r_u8(r); it->state = r_u8(r);
  it->owner = (u32)r_var(r);
  it->z = r_u16(r);
}

static void read_ent(Reader *r, Ent *e) {
  memset(e, 0, sizeof *e);
  e->used = true;
  e->id = (u32)r_var(r);
  r_str(r, e->name, sizeof e->name);
  e->x = r_u8(r); e->y = r_u8(r); e->dir = r_u8(r); e->flags = r_u8(r);
  e->look = r_var(r);
  e->z = r_u16(r);
  e->guest = (u32)r_var(r);
  e->fx = e->x; e->fy = e->y; e->fz = e->z;
  e->next_x = e->next_y = 255;
  e->move_dir = -1;
}

static Ent *new_ent(void) {
  for (int i = 0; i < MAX_ENTS; i++) if (!W.ents[i].used) return &W.ents[i];
  return NULL;
}

static Item *put_item(const Item *src) {
  Item *it = world_item(src->id);
  for (int i = 0; !it && i < MAX_ITEMS; i++) if (!W.items[i].used) it = &W.items[i];
  if (it) {
    Item next = *src;
    if (it->used && !next.slide_at && it->x == next.x && it->y == next.y && it->z == next.z) {
      next.fx = it->fx; next.fy = it->fy; next.fz = it->fz; next.slide_at = it->slide_at;
    }
    *it = next;
  }
  return it;
}

void item_pos(const Item *it, u64 now, float *x, float *y, float *z) {
  float t = it->slide_at ? progress(it->slide_at, now) : 1;
  *x = it->fx + (it->x - it->fx) * t;
  *y = it->fy + (it->y - it->fy) * t;
  *z = it->fz + (it->z - it->fz) * t;
}

void world_handle(const u8 *data, int len) {
  Reader r;
  r_init(&r, data, len);
  u64 now = now_ms();
  char text[512];
  switch (r_u8(&r)) {
    case S_WELCOME:
      W.me = (u32)r_var(&r);
      r_str(&r, W.name, sizeof W.name);
      r_str(&r, W.token, sizeof W.token);
      W.credits = (u32)r_var(&r);
      W.guest_id = (u32)r_var(&r);
      break;
    case S_ROOM_TITLE: r_str(&r, W.title, sizeof W.title); break;
    case S_ROOM: {
      W.room_rights = W.room_owner = false;
      r_str(&r, W.slug, sizeof W.slug);
      r_str(&r, W.title, sizeof W.title);
      r_str(&r, W.model, sizeof W.model);
      W.w = r_u8(&r); W.h = r_u8(&r);
      static char rows[MAX_SIDE * (MAX_SIDE + 1) + 1];
      r_str(&r, rows, sizeof rows);
      memset(W.rows, 'x', sizeof W.rows);
      if (W.w > MAX_SIDE) W.w = MAX_SIDE;
      if (W.h > MAX_SIDE) W.h = MAX_SIDE;
      const char *p = rows;
      for (int y = 0; y < W.h && *p; y++) {
        for (int x = 0; x < W.w && *p && *p != '\n'; x++) W.rows[y * W.w + x] = *p++;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
      }
      W.door_x = r_u8(&r); W.door_y = r_u8(&r);
      W.owner = (u32)r_var(&r);
      r_str(&r, W.owner_name, sizeof W.owner_name);
      memset(W.items, 0, sizeof W.items);
      for (int n = (int)r_var(&r), i = 0; n > 0 && !r.err; n--) {
        Item it; read_item(&r, &it);
        if (i < MAX_ITEMS) W.items[i++] = it;
      }
      memset(W.ents, 0, sizeof W.ents);
      for (int n = (int)r_var(&r), i = 0; n > 0 && !r.err; n--) {
        Ent e; read_ent(&r, &e);
        if (i < MAX_ENTS) W.ents[i++] = e;
      }
      W.wall[0] = W.floor[0] = 0;
      memset(W.invites, 0, sizeof W.invites);
      memset(&W.duel, 0, sizeof W.duel);
      W.game_rev++;
      W.in_room = true;
      on_room();
      break;
    }
    case S_ENTITY_ADD: {
      Ent e; read_ent(&r, &e);
      Ent *old = world_ent(e.id), *slot = old ? old : new_ent();
      if (slot) *slot = e;
      break;
    }
    case S_ENTITY_REMOVE: {
      Ent *e = world_ent((u32)r_var(&r));
      if (e) e->used = false;
      break;
    }
    case S_TYPING: {
      Ent *e=world_ent((u32)r_var(&r));
      u8 active=r_u8(&r);
      if(e&&!r.err)e->typing_until=active?now+8000:0;
      break;
    }
    case S_ACTION:
      // Expressions and dance styles: id, action, dance style, remaining milliseconds.
      for (int n = (int)r_var(&r); n > 0 && !r.err; n--) {
        Ent *e = world_ent((u32)r_var(&r));
        u8 action = r_u8(&r), dance = r_u8(&r);
        u32 left = (u32)r_var(&r);
        if (e && !r.err) { e->action = action; e->dance = dance; e->action_until = left ? now + left : 0; }
      }
      break;
    case S_DEVICE:
      // Native device of each player (3DS/Switch/Vita); old servers never send it.
      for (int n = (int)r_var(&r); n > 0 && !r.err; n--) {
        Ent *e = world_ent((u32)r_var(&r));
        u8 device = r_u8(&r);
        if (e && !r.err) e->device = device;
      }
      break;
    case S_DELTAS:
      for (int n = (int)r_var(&r); n > 0 && !r.err; n--) {
        u32 id = (u32)r_var(&r);
        u8 x = r_u8(&r), y = r_u8(&r), dir = r_u8(&r), flags = r_u8(&r);
        u8 nx = r_u8(&r), ny = r_u8(&r);
        u16 z = r_u16(&r);
        Ent *e = world_ent(id);
        if (!e) continue;
        if (e->x != x || e->y != y || e->z != z) {
          e->move_dir = (s8)(((int)lroundf(atan2f((float)(x - e->x), (float)-(y - e->y)) / ((float)M_PI / 4)) + 8) & 7);
          float px, py;
          ent_pos(e, now, &px, &py);
          e->fz = ent_height(e, now);
          e->fx = px; e->fy = py; e->t0 = now;
          e->x = x; e->y = y; e->z = z;
        }
        e->dir = dir; e->flags = flags; e->next_x = nx; e->next_y = ny;
      }
      break;
    case S_CHAT: {
      u32 eid = (u32)r_var(&r);
      r_str(&r, text, sizeof text);
      if (!r.err) on_chat(eid, text);
      break;
    }
    case S_CREDITS: W.credits = (u32)r_var(&r); W.inv_rev++; break;
    case S_INVENTORY:
      W.inv_n = 0;
      for (int n = (int)r_var(&r); n > 0 && !r.err; n--) {
        u32 id = (u32)r_var(&r);
        u16 def = r_u16(&r);
        if (!r.err && W.inv_n < MAX_INV) W.inv[W.inv_n++] = (InvItem){id, def};
      }
      W.inv_rev++;
      break;
    case S_INV_ADD: { u32 id = (u32)r_var(&r); u16 def = r_u16(&r); if (!r.err) inv_add(id, def); break; }
    case S_INV_REMOVE: { u32 id = (u32)r_var(&r); if (!r.err) inv_remove(id); break; }
    case S_TRANSFER: {
      char slug[64], arrival[80];
      r_str(&r, slug, sizeof slug);
      r_str(&r, arrival, sizeof arrival);
      if (!r.err) on_transfer(public_slug(slug), arrival);
      break;
    }
    case S_HOTEL_ALERT: {
      // Alerts allow 1000 characters (up to ~4 KB of escaped UTF-8 JSON).
      static char alert[8192];
      r_str(&r, alert, sizeof alert);
      if (!r.err) on_alert(alert);
      break;
    }
    case S_SOCIAL_CHANGED: on_social_changed(); break;
    case S_PETS: set_json(&W.pets_json, &r); break;
    case S_PET_INVENTORY: set_json(&W.pet_inv_json, &r); break;
    case S_GAME: {
      u8 op = r_u8(&r);
      if (op == 1 || op == 4) {
        u32 from = (u32)r_var(&r); u8 kind = op == 1 ? r_u8(&r) : 0;
        if (r.err || (op == 1 && (kind < 1 || kind > 3))) break;
        int slot = -1;
        for (int i = 0; i < 8; i++) if (W.invites[i].from == from) { slot = i; break; }
        if (slot < 0 && op == 1) for (int i = 0; i < 8; i++) if (!W.invites[i].from) { slot = i; break; }
        if (slot >= 0) W.invites[slot] = (GameInvite){op == 1 ? from : 0, kind};
      } else if (op == 2) {
        Duel game = {0};
        game.id = (u32)r_var(&r); game.kind = r_u8(&r);
        game.players[0] = (u32)r_var(&r); game.players[1] = (u32)r_var(&r);
        game.turn = r_u8(&r); game.result = r_u8(&r); game.reason = r_u8(&r);
        game.last[0] = r_u8(&r); game.last[1] = r_u8(&r); game.check = r_u8(&r);
        u64 n = r_var(&r);
        if (game.kind < 1 || game.kind > 3 || game.turn > 1 || game.result > 3 || n != (game.kind == 1 ? 9u : game.kind == 2 ? 42u : 64u)) break;
        for (unsigned i = 0; i < n; i++) game.board[i] = r_u8(&r);
        u64 moves = r_var(&r);
        if (moves > 256) break;
        game.move_count = (u16)moves;
        for (unsigned i = 0; i < moves; i++) { game.moves[i][0] = r_u8(&r); game.moves[i][1] = r_u8(&r); }
        if (r.err) break;
        W.duel = game;
      } else break;
      W.game_rev++;
      break;
    }
    case S_OWNER_IDENTITY: set_json(&W.owners_json, &r); W.owners_rev++; break;
    case S_ROOM_RIGHTS: {
      set_json(&W.rights_json, &r);
      // Compact JSON from the server; the lists are read by the room rights window.
      W.room_rights = W.rights_json && strstr(W.rights_json, "\"rights\":true");
      W.room_owner = W.rights_json && strstr(W.rights_json, "\"owner\":true");
      W.rights_rev++;
      break;
    }
    case S_KICKED: r_str(&r, text, sizeof text); if (!r.err) on_kicked(text); break;
    case S_CASINO: {
      char *event = NULL; set_json(&event, &r);
      if (event) { on_casino(event); free(event); }
      break;
    }
    case S_TRADE: {
      char *event = NULL; set_json(&event, &r);
      if (event) { on_trade(event); free(event); }
      break;
    }
    case S_MUSIC: {
      char *event = NULL; set_json(&event, &r);
      if (event) { on_music(event); free(event); }
      break;
    }
    case S_ACTIVITY: {
      char *event = NULL; set_json(&event, &r);
      if (event) { on_activity(event); free(event); }
      break;
    }
    case S_SNOW: on_snow(r.b+r.p,r.len-r.p); break;
    case S_FINISH:
      r_str(&r, W.wall, sizeof W.wall);
      r_str(&r, W.floor, sizeof W.floor);
      W.finish_rev++;
      break;
    case S_ONLINE: W.online = (int)r_var(&r); break;
    case S_ITEM_ADD:
    case S_ITEM_UPDATE: { Item it; read_item(&r, &it); if (!r.err) { put_item(&it); on_item(it.id); } break; }
    case S_SLIDE:
      for (int n = (int)r_var(&r); n > 0 && !r.err; n--) {
        Item it; read_item(&r, &it);
        if (r.err) break;
        const Item *old = world_item(it.id);
        if (old) { item_pos(old, now, &it.fx, &it.fy, &it.fz); it.slide_at = now; }
        put_item(&it);
      }
      break;
    case S_ITEM_REMOVE: { Item *it = world_item((u32)r_var(&r)); if (it) it->used = false; break; }
    case S_NOTICE: r_str(&r, text, sizeof text); if (!r.err) on_notice(text); break;
    case S_ENTITY_NAME: {
      Ent *e = world_ent((u32)r_var(&r));
      r_str(&r, text, sizeof text);
      if (e && !r.err) {
        snprintf(e->name, sizeof e->name, "%s", text);
        if (e->id == W.me) snprintf(W.name, sizeof W.name, "%s", text);
        if (e->guest == W.owner) snprintf(W.owner_name, sizeof W.owner_name, "%s", text);
        on_name_changed(e->id);
      }
      break;
    }
    case S_ENTITY_LOOK: {
      Ent *e = world_ent((u32)r_var(&r));
      u64 look = r_var(&r);
      if (e && !r.err) e->look = look;
      break;
    }
    case S_CARRY: {
      Ent *e = world_ent((u32)r_var(&r));
      u8 drink = r_u8(&r);
      u64 duration = r_var(&r);
      if (e && !r.err) { e->drink = drink; e->drink_until = now + duration; }
      break;
    }
    default:
      // Messages for features the 3DS does not show (server catalogue list…).
      break;
  }
}
