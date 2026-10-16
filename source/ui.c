// Bottom screen with the classic look of the web edition: header with room,
// players and credits; icon bar (Me, Navigator, Catalogue, Hand,
// Friends, Chat, Help) and steel-blue windows for each panel.
#include "ui.h"
#include <3ds.h>
#include <citro2d.h>
#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "avatar.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "pets.h"
#include "proto.h"
#include "scene.h"
#include "ui_int.h"
#include "snow.h"
#include "osk.h"
#include "world.h"
#include "update.h"

#define LOG_MAX 48

Keys K;

typedef struct { char name[24], text[104]; u32 color; } LogLine;

static struct {
  Panel panel;
  u32 item, ent;
  char toast[160];
  u64 toast_until;
  LogLine log[LOG_MAX];
  int log_n;
  ListState chat_list;
  char last_slug[64];
  // Wardrobe
  Look draft;
  bool draft_dirty;
  int preview_dir;
  int look_tab;
  // Walking with the pad
  int stick_dir;
  u64 stick_sent;
  int sent_x, sent_y;
  // Minimap
  float mm_tile, mm_ox, mm_oy;
  // Hotel alerts (queue)
  struct { char title[96], text[4096]; int id; bool capacity; } alerts[4];
  int alert_n, alert_page, alert_offsets[64];
  bool alert;
  int seen_alerts[32];
  int seen_n;
  // Modal confirmation
  bool confirm;
  char confirm_text[160], confirm_yes[24];
  void (*confirm_fn)(void *);
  void *confirm_ctx;
  // Identity
  bool show_code;
  int identity_job;
  char identity_token[96], identity_name[32];
  // One-time move to another device (server: cloud/transfer.rs)
  int move_job, move_status_job, move_claim_job;
  bool move_confirming;
  char move_code[16], move_typed[16], move_name[32];
  u64 move_expires, move_polled;
  ListState settings_list;
} U = {.stick_dir = -1, .preview_dir = 2};

// ---------------------------------------------------------------- helpers

void ui_open(Panel p) {
  if (p != P_CASINO) casino_close(true);
  if (p != P_SNOW) snow_close(true);
  if (p != P_SHOP) spaces_reset(true);
  if (p == P_NAV) nav_opened();
  if (p == P_FRIENDS) friends_opened();
  if (p == P_LOOK) U.draft_dirty = false;
  U.panel = p;
}
Panel ui_panel(void) { return U.panel; }
void ui_close(void) { snow_close(true); casino_close(true); spaces_reset(true); U.panel = P_ROOM; }

void ui_select_item(u32 id) { U.item = id; ui_open(P_ITEM); }
void ui_select_ent(u32 eid) {
  scene_select(eid);
  if (eid == W.me) { ui_open(P_ME); return; }
  U.ent = eid;
  ui_open(P_ENT);
}

static bool confirm_opened;

void ui_confirm(const char *text, const char *yes, void (*fn)(void *), void *ctx) {
  snprintf(U.confirm_text, sizeof U.confirm_text, "%s", text);
  snprintf(U.confirm_yes, sizeof U.confirm_yes, "%s", yes);
  U.confirm_fn = fn;
  U.confirm_ctx = ctx;
  U.confirm = true;
  // The press that opened the dialog must not also answer it in the same frame
  // (B used to open "Leave?" and cancel it at once).
  memset(&K, 0, sizeof K);
  T.released = false;
  confirm_opened = true;
}

// ---------------------------------------------------------------- world events

void ui_room_changed(void) {
  casino_close(false);
  snow_close(false);
  spaces_reset(false);
  // A connection renewal sends ROOM again: the chat is only cleared when the room changes.
  if (strcmp(U.last_slug, W.slug)) {
    U.log_n = 0;
    snprintf(U.last_slug, sizeof U.last_slug, "%s", W.slug);
    if (U.panel == P_ITEM || U.panel == P_ENT || U.panel == P_GAME || U.panel == P_PROFILE) U.panel = P_ROOM;
    build_cancel();
  }
  U.draft_dirty = false;
}

void ui_chat(u32 eid, const char *text) {
  Ent *e = world_ent(eid);
  if (U.log_n == LOG_MAX) { memmove(U.log, U.log + 1, sizeof(LogLine) * (LOG_MAX - 1)); U.log_n--; }
  LogLine *l = &U.log[U.log_n++];
  snprintf(l->name, sizeof l->name, "%s", e ? e->name : "?");
  snprintf(l->text, sizeof l->text, "%s", text);
  u32 c = e ? look_shirt_rgb(e->look) : SK_DIM;
  // On the light-grey panel, very light shirts are darkened to stay readable.
  int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
  if (r + g + b > 480) { r = r * 5 / 10; g = g * 5 / 10; b = b * 5 / 10; }
  l->color = (u32)r << 16 | (u32)g << 8 | (u32)b;
  U.chat_list.scroll = 1e9f;
}

bool ui_chat_blocked(u32 eid) {
  Ent *e = world_ent(eid);
  return e && friends_is_blocked(e->guest);
}

void ui_toast(const char *text, u64 now) {
  utf8_copy(U.toast, sizeof U.toast, text);
  U.toast_until = now + 3500;
}

void ui_notice(const char *text) { (void)text; build_notice(); }
void ui_item_changed(u32 id) { build_result(id); }

void ui_alert(const char *text) {
  json_error_t err;
  json_t *j = json_loads(text, 0, &err);
  if (!j) return;
  int id = (int)json_integer_value(json_object_get(j, "id"));
  const char *title = json_string_value(json_object_get(j, "title")), *msg = json_string_value(json_object_get(j, "message"));
  bool seen = false;
  for (int i = 0; i < U.seen_n; i++) seen |= U.seen_alerts[i] == id;
  for (int i = 0; i < U.alert_n; i++) seen |= U.alerts[i].id == id;
  // Queued like the web: each alert stays until the player presses OK.
  if (title && msg && !seen && U.alert_n < COUNT(U.alerts)) {
    snprintf(U.alerts[U.alert_n].title, sizeof U.alerts[0].title, "%s", title);
    snprintf(U.alerts[U.alert_n].text, sizeof U.alerts[0].text, "%s", msg);
    U.alerts[U.alert_n].id = id;
    const char *style = json_string_value(json_object_get(j, "style"));
    U.alerts[U.alert_n].capacity = style && !strcmp(style, "capacity");
    U.alert_n++;
    U.alert = true;
  }
  json_decref(j);
}

void ui_social_changed(void) { friends_poll(0); }
void ui_name_changed(u32 eid) {
  if (eid == W.me) ui_toast("Name saved. Your character and rooms are unchanged.", now_ms());
}

// ---------------------------------------------------------------- actions

static void chat_done(const char *text, void *ctx) { (void)ctx; send_chat(text); }
static void unlock_done(const char *text, void *ctx) { (void)ctx; net_unlock(text); }

static void talk(void) {
  // Touch keyboard: the room keeps moving on the top screen while you type.
  osk_open("Say something… (START: send)", NULL, 100, chat_done, NULL);
}

static void rename_done(const char *text, void *ctx) { (void)ctx; if (strcmp(text, W.name)) send_name(text); }
static void rename_me(void) { osk_open("New name (3-20: letters, numbers, _ -)", W.name, 20, rename_done, NULL); }

static void get_drink(u64 now) {
  Ent *me = world_ent(W.me);
  Item *best = NULL;
  int best_d = 1 << 30;
  for (int i = 0; i < MAX_ITEMS && me; i++) {
    Item *it = &W.items[i];
    const FurniDef *d = it->used ? def_get(it->def) : NULL;
    if (!d || !d->drink) continue;
    int dist = (it->x - me->x) * (it->x - me->x) + (it->y - me->y) * (it->y - me->y);
    if (dist < best_d) { best_d = dist; best = it; }
  }
  const FurniDef *d = best ? def_get(best->def) : NULL;
  if (best) send_drink(best->id, d->drink);
  else ui_toast("There is no drink dispenser in this room.", now);
}

// ---------------------------------------------------------------- "Press A" (3DS only)

// Without double taps or a mouse, the console offers the action of the item (or person) next to you:
// the prompt appears on the top screen and A runs it. With nothing nearby, A still talks.
typedef struct { const Item *item; const Ent *ent; char label[64]; } Context;

static const s8 FACING[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

static bool seat_taken(const Item *it) {
  for (int i = 0; i < MAX_ENTS; i++)
    if (W.ents[i].used && W.ents[i].id != W.me && W.ents[i].x == it->x && W.ents[i].y == it->y) return true;
  return false;
}

static bool context_find(Context *c, u64 now) {
  memset(c, 0, sizeof *c);
  Ent *me = world_ent(W.me);
  if (!me || ent_moving(me, now) || build_active()) return false;
  int fx = me->x + FACING[me->dir & 7][0], fy = me->y + FACING[me->dir & 7][1];
  int best = 1 << 30;
  for (int i = 0; i < MAX_ITEMS; i++) {
    const Item *it = &W.items[i];
    const FurniDef *d = it->used ? def_get(it->def) : NULL;
    if (!d || d->kind == KIND_WALL) continue;
    const char *verb = NULL;
    if (d->casino) verb = "Play";
    else if (d->drinks && d->drinks[0]) verb = (me->drink && now < me->drink_until) ? NULL : "Get a drink";
    else if (d->kind == KIND_DICE) verb = "Roll the dice";
    else if (d->flags & DEF_TELEPORTER) verb = "Enter the teleporter";
    else if (d->flags & DEF_GATE) verb = it->state ? "Close the gate" : "Open the gate";
    else if (d->kind == KIND_TOGGLE) verb = it->state ? "Switch off" : "Switch on";
    else if (d->flags & DEF_LAY) verb = "Lie down";
    else if (d->kind == KIND_SIT) verb = (me->flags & FLAG_SIT) && me->x == it->x && me->y == it->y ? NULL : seat_taken(it) ? NULL : "Sit";
    if (!verb) continue;
    int w, h;
    def_size(it->def, it->rot, &w, &h);
    // Chebyshev distance from the player to the furniture footprint.
    int dx = me->x < it->x ? it->x - me->x : me->x >= it->x + w ? me->x - (it->x + w - 1) : 0;
    int dy = me->y < it->y ? it->y - me->y : me->y >= it->y + h ? me->y - (it->y + h - 1) : 0;
    int dist = dx > dy ? dx : dy;
    if (dist > 1) continue;
    bool facing = fx >= it->x && fy >= it->y && fx < it->x + w && fy < it->y + h;
    // Casino machines sit behind their chairs: offer the game first (the server seats the player).
    int score = dist * 10 - (facing ? 6 : 0) - (d->casino ? 15 : 0);
    if (score < best) {
      best = score;
      c->item = it;
      snprintf(c->label, sizeof c->label, "%s · %s", verb, d->name);
    }
  }
  // Someone right in front: view their profile.
  for (int i = 0; i < MAX_ENTS && !c->item; i++) {
    const Ent *e = &W.ents[i];
    if (e->used && e->id != W.me && e->x == fx && e->y == fy) { c->ent = e; snprintf(c->label, sizeof c->label, "View %s", e->name); }
  }
  return c->item || c->ent;
}

static void context_run(const Context *c, u64 now) {
  if (c->ent) { ui_select_ent(c->ent->id); return; }
  const Item *it = c->item;
  const FurniDef *d = def_get(it->def);
  if (d->casino) send_use(it->id);
  else if (d->drinks && d->drinks[0]) send_drink(it->id, atoi(d->drinks));
  else if (d->kind == KIND_SIT || (d->flags & DEF_LAY)) { send_walk(it->x, it->y); scene_mark_destination(it->x, it->y, now); }
  else send_use(it->id);
}

// ---------------------------------------------------------------- walking with the pad

static const s8 SCREEN_STEP[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};

static bool passable(int x, int y, bool goal) {
  if (!world_tile(x, y)) return false;
  Item *it = world_item_at(x, y);
  const FurniDef *d = it ? def_get(it->def) : NULL;
  if (!d) return true;
  if (d->kind == KIND_FLOOR) return true;
  if (d->flags & DEF_GATE) return it->state > 0;
  if (d->kind == KIND_SIT || (d->flags & (DEF_LAY | DEF_TELEPORTER))) return goal;
  return false;
}

static int pad_sector(u32 held, bool dpad) {
  circlePosition cp;
  hidCircleRead(&cp);
  float dx = cp.dx, dy = cp.dy;
  if (dpad) {
    if (held & KEY_DUP) dy += 150;
    if (held & KEY_DDOWN) dy -= 150;
    if (held & KEY_DRIGHT) dx += 150;
    if (held & KEY_DLEFT) dx -= 150;
  }
  if (dx * dx + dy * dy < 45 * 45) return -1;
  return ((int)lroundf(atan2f(dx, dy) / ((float)M_PI / 4)) + 8) & 7;
}

static void stick_walk(u32 held, bool dpad, u64 now) {
  Ent *me = world_ent(W.me);
  int s = pad_sector(held, dpad);
  if (!me) return;
  if (s < 0) {
    if (U.stick_dir >= 0 && me->next_x != 255) send_walk(me->next_x, me->next_y);
    U.stick_dir = -1;
    return;
  }
  if (s == U.stick_dir && now - U.stick_sent < 350) return;
  int dx = SCREEN_STEP[s][0], dy = SCREEN_STEP[s][1];
  int x1 = me->x + dx, y1 = me->y + dy, x2 = x1 + dx, y2 = y1 + dy, tx = -1, ty = -1;
  if (passable(x1, y1, false) && passable(x2, y2, true)) { tx = x2; ty = y2; }
  else if (passable(x1, y1, true)) { tx = x1; ty = y1; }
  U.stick_dir = s;
  U.stick_sent = now;
  if (tx >= 0 && !(tx == U.sent_x && ty == U.sent_y && me->next_x != 255)) {
    send_walk(tx, ty);
    U.sent_x = tx; U.sent_y = ty;
  }
}

// ---------------------------------------------------------------- Room panel (minimap)

#define MM_Y (HEAD_H + 2)
#define MM_H (BAR_Y - HEAD_H - 28)

static void minimap_fit(void) {
  int d0 = 1 << 20, d1 = -(1 << 20), s0 = 1 << 20, s1 = -(1 << 20);
  for (int y = 0; y < W.h; y++)
    for (int x = 0; x < W.w; x++) {
      if (!world_tile(x, y)) continue;
      if (x - y < d0) d0 = x - y;
      if (x - y > d1) d1 = x - y;
      if (x + y < s0) s0 = x + y;
      if (x + y > s1) s1 = x + y;
    }
  if (d1 < d0) { d0 = d1 = s0 = s1 = 0; }
  float area_w = 308, area_h = MM_H - 4;
  float span_w = (d1 - d0 + 2) / 2.0f, span_h = (s1 - s0 + 2) / 4.0f;
  U.mm_tile = fminf(area_w / span_w, area_h / span_h);
  if (U.mm_tile > 40) U.mm_tile = 40;
  float half = U.mm_tile / 2, quarter = U.mm_tile / 4;
  float left = (d0 - 1) * half, top = s0 * quarter;
  float w = (d1 - d0 + 2) * half, h = (s1 - s0 + 2) * quarter;
  U.mm_ox = (BOT_W - w) / 2 - left;
  U.mm_oy = MM_Y + 2 + (area_h - h) / 2 - top;
}

static void mm_point(float x, float y, float *sx, float *sy) {
  *sx = U.mm_ox + (x - y) * U.mm_tile / 2;
  *sy = U.mm_oy + (x + y) * U.mm_tile / 4;
}

static void mm_diamond(int x, int y, float inset, u32 c) {
  float sx, sy, h = U.mm_tile / 2 - inset, q = U.mm_tile / 4 - inset / 2;
  mm_point(x, y, &sx, &sy);
  sy += U.mm_tile / 4;
  C2D_DrawTriangle(sx, sy - q, c, sx + h, sy, c, sx - h, sy, c, 0.5f);
  C2D_DrawTriangle(sx - h, sy, c, sx + h, sy, c, sx, sy + q, c, 0.5f);
}

/** True when no floor tile of the minimap overlaps the rectangle. */
static bool mm_corner_free(float x, float y, float w, float h) {
  float half = U.mm_tile / 2, quarter = U.mm_tile / 4;
  for (int ty = 0; ty < W.h; ty++)
    for (int tx = 0; tx < W.w; tx++) {
      if (!world_tile(tx, ty)) continue;
      float sx, sy;
      mm_point(tx, ty, &sx, &sy);
      if (sx + half > x && sx - half < x + w && sy + 2 * quarter > y && sy < y + h) return false;
    }
  return true;
}

static void tap_tile(int x, int y, u64 now) {
  if (build_active()) { build_tap_tile(x, y); return; }
  for (int i = 0; i < MAX_ENTS; i++) {
    Ent *e = &W.ents[i];
    if (e->used && e->x == x && e->y == y) { ui_select_ent(e->id); return; }
  }
  scene_select(0);
  Item *it = world_item_at(x, y);
  const FurniDef *d = it ? def_get(it->def) : NULL;
  if (d && d->kind != KIND_FLOOR && (it->owner || d->drink || d->kind == KIND_TOGGLE || d->kind == KIND_DICE ||
                                     (d->flags & DEF_TELEPORTER))) {
    // Interactive items open their panel, like clicking them on the web.
    ui_select_item(it->id);
    if (d->kind == KIND_SIT || (d->flags & DEF_LAY)) { send_walk(x, y); scene_mark_destination(x, y, now); }
    return;
  }
  send_walk(x, y);
  scene_mark_destination(x, y, now);
}

static bool corner_tool(float x, float y, float w, float h, const char *icon, const char *label, bool enabled) {
  bool down = enabled && T.held && T.x0 >= x && T.y0 >= y && T.x0 < x + w && T.y0 < y + h;
  fill_round(x, y, w, h, 5, sk_rgb(0x5d6a72));
  fill_round(x + 1, y + 1, w - 2, h - 2, 4, sk_rgb(down ? 0x1c2830 : 0x2c3b44));
  int iw = 0, ih = 0;
  if (sk_sprite_size(icon, &iw, &ih)) {
    float k = ih > 24 ? 24.f / ih : 1;
    sk_sprite(icon, roundf(x + (w - iw * k) / 2), roundf(y + 3 + (24 - ih * k) / 2), k);
  }
  sk_text(label, x + w / 2, y + h - 13, 9, enabled ? SK_WHITE : 0x8a959c, 1, w - 2);
  return enabled && sk_hit(x, y, w, h);
}

static void panel_room(u64 now) {
  minimap_fit();
  // Background like the room floor, with the original grid.
  u32 floor_a = sk_rgb(0x989865), floor_b = sk_rgb(0x9f9f6c);
  for (int y = 0; y < W.h; y++)
    for (int x = 0; x < W.w; x++)
      if (world_tile(x, y)) mm_diamond(x, y, 0.6f, scene_water_tile(x, y) ? sk_rgb((x + y) % 2 ? 0x3fa9d6 : 0x46b0dc) : (x + y) % 2 ? floor_a : floor_b);
  if (!world_tile(W.door_x, W.door_y)) mm_diamond(W.door_x, W.door_y, 2, sk_rgb(0x17130f));
  for (int i = 0; i < MAX_ITEMS; i++) {
    Item *it = &W.items[i];
    const FurniDef *d = it->used ? def_get(it->def) : NULL;
    if (!d || d->kind == KIND_WALL) continue;
    u32 c = d->drink ? 0xe39b3b : d->kind == KIND_SIT ? 0x4f8fcf : d->kind == KIND_FLOOR ? 0x7f9a6a
          : d->kind == KIND_TOGGLE ? 0xd9c25a : 0x4a5a64;
    if (it->id == U.item && U.panel == P_ITEM) c = 0xffffff;
    int w, h;
    def_size(it->def, it->rot, &w, &h);
    for (int ty = it->y; ty < it->y + h; ty++)
      for (int tx = it->x; tx < it->x + w; tx++) mm_diamond(tx, ty, 2.5f, sk_rgb(c));
  }
  if (G.on && !G.wall) {
    int w, h;
    def_size(G.def, (u8)G.rot, &w, &h);
    for (int ty = G.y; ty < G.y + h; ty++)
      for (int tx = G.x; tx < G.x + w; tx++) mm_diamond(tx, ty, 1.5f, sk_rgba(G.ok ? 0x7bd389 : 0xff6b6b, 220));
  }
  pets_sync(now);
  for (int i = 0; i < pets_count(); i++) {
    const Pet *pt = pet_at(i);
    float px, py, sx, sy;
    pet_pos(pt, now, &px, &py);
    mm_point(px, py, &sx, &sy);
    sy += U.mm_tile / 4;
    int k = pt->kind < pet_kind_count ? pt->kind : 0;
    C2D_DrawCircleSolid(sx, sy, 0.5f, fmaxf(2.5f, U.mm_tile * .14f) + 1, sk_rgb(0x0d1418));
    C2D_DrawCircleSolid(sx, sy, 0.5f, fmaxf(2.5f, U.mm_tile * .14f), sk_rgb(pet_kinds[k].colors[pt->color % pet_kinds[k].color_count]));
  }
  for (int i = 0; i < MAX_ENTS; i++) {
    Ent *e = &W.ents[i];
    if (!e->used) continue;
    float px, py, sx, sy;
    ent_pos(e, now, &px, &py);
    mm_point(px, py, &sx, &sy);
    sy += U.mm_tile / 4;
    float r = fmaxf(3, U.mm_tile * .2f);
    if (e->id == W.me) C2D_DrawCircleSolid(sx, sy, 0.5f, r + 2.5f, sk_rgb(SK_ACCENT));
    else if (e->id == scene_selected()) C2D_DrawCircleSolid(sx, sy, 0.5f, r + 2.5f, sk_rgb(SK_WHITE));
    C2D_DrawCircleSolid(sx, sy, 0.5f, r + 1, sk_rgb(0x0d1418));
    C2D_DrawCircleSolid(sx, sy, 0.5f, r, sk_rgb(look_shirt_rgb(e->look)));
    if (e->flags & FLAG_3DS) console_badge(sx + r - 1, sy - r - 8, 0.6f);
  }

  // Social accounts as quiet text in the bottom corners of the map, only where no floor is drawn.
  {
    float sy = MM_Y + MM_H - 24;
    const float lw = 100, rw = 112;
    if (mm_corner_free(0, sy - 2, lw, 26)) {
      sk_text("Follow us", 6, sy, 9, 0x8a959c, 0, lw);
      sk_text("@Tweebbo", 6, sy + 11, 9.5f, 0xc9d3d8, 0, lw);
    }
    if (mm_corner_free(BOT_W - rw, sy - 2, rw, 26)) {
      sk_text("TikTok · Instagram", BOT_W - 6, sy, 9, 0xc9d3d8, 2, rw);
      sk_text("X · YouTube", BOT_W - 6, sy + 11, 9, 0xc9d3d8, 2, rw);
    }
  }
  // Games and Casino in the empty top corners of the map, like the web's Games and Casino tools.
  const float cw = 44, ch = 40, cy = MM_Y + 3;
  bool corner = T.y0 >= cy && T.y0 < cy + ch && (T.x0 < 4 + cw || T.x0 >= BOT_W - 4 - cw);
  if (corner_tool(4, cy, cw, ch, "games-icon", "Games", net_online())) ui_open(P_GAMES);
  if (corner_tool(BOT_W - 4 - cw, cy, cw, ch, "casino-icon", "Casino", net_online())) {
    if (strcmp(W.slug, "casino")) net_goto("casino", "");
    else ui_toast("Walk up to a machine or table and press A to play.", now);
  }
  if (!corner && T.released && !T.dragging && T.y >= MM_Y && T.y < MM_Y + MM_H) {
    float u = (T.x - U.mm_ox) / (U.mm_tile / 2), v = (T.y - U.mm_oy) / (U.mm_tile / 4);
    int x = (int)floorf((u + v) / 2), y = (int)floorf((v - u) / 2);
    if (world_tile(x, y)) tap_tile(x, y, now);
  }

  // Quick actions, like the room bar buttons on the web.
  Ent *me = world_ent(W.me);
  bool sitting = me && (me->flags & FLAG_SIT), dancing = me && (me->flags & FLAG_DANCE);
  float y = BAR_Y - 24, w = 74, gap = (BOT_W - 8 - w * 4) / 3;
  if (sk_button(4, y, w, "Wave (X)", true)) send_gesture(GESTURE_WAVE);
  if (sk_button(4 + (w + gap), y, w, dancing ? "Stop (Y)" : "Dance (Y)", true)) send_gesture(GESTURE_DANCE);
  if (sk_button(4 + (w + gap) * 2, y, w, sitting ? "Stand (B)" : "Sit (B)", true)) send_gesture(GESTURE_SIT);
  if (sk_button(4 + (w + gap) * 3, y, w, "Say (A)", true)) talk();
}

// ---------------------------------------------------------------- Chat panel

static void panel_chat(u64 now) {
  (void)now;
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Chat")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H - 24);
  const float row = 15;
  int first = sk_list_begin(&U.chat_list, IN_X + 2, IN_Y + 2, IN_W - 4, IN_H - 28, U.log_n, row);
  for (int i = first; i < U.log_n; i++) {
    float y = sk_list_row_y(i);
    if (y > IN_Y + IN_H) break;
    LogLine *l = &U.log[i];
    char name[32];
    snprintf(name, sizeof name, "%s:", l->name);
    float nw = sk_text(name, IN_X + 6, y + 1, 10.5f, l->color, 0, 110);
    text_draw(name, IN_X + 6.6f, y + 1, 10.5f, sk_rgb(l->color), 0);
    sk_text(l->text, IN_X + 10 + nw, y + 1, 10.5f, SK_TEXT, 0, IN_W - 22 - nw);
  }
  sk_list_end();
  if (!U.log_n) sk_text("Nobody has spoken yet.", BOT_W / 2, IN_Y + 60, 11, SK_DIM, 1, 0);
  if (sk_field(IN_X, IN_Y + IN_H - 20, IN_W - 64, "", "Type here…  (A)") ||
      sk_button(IN_X + IN_W - 58, IN_Y + IN_H - 20, 58, "Say", true))
    talk();
}

// ---------------------------------------------------------------- Me panel

static void identity_code(void *ctx);

static void use_character(void *ctx) {
  (void)ctx;
  net_use_token(U.identity_token);
  memset(U.identity_token, 0, sizeof U.identity_token);
  ui_toast("Character restored. Reconnecting…", now_ms());
  ui_close();
}

static void identity_poll(u64 now) {
  HttpResult r;
  if (!U.identity_job || !net_http_result(U.identity_job, &r)) return;
  U.identity_job = 0;
  json_error_t err;
  json_t *j = r.body ? json_loads(r.body, 0, &err) : NULL;
  if (r.status == 200 && j && json_string_value(json_object_get(j, "name"))) {
    snprintf(U.identity_name, sizeof U.identity_name, "%s", json_string_value(json_object_get(j, "name")));
    char text[160];
    snprintf(text, sizeof text, "Use the character \"%s\" on this console? Save your current code first.", U.identity_name);
    ui_confirm(text, "Use this character", use_character, NULL);
  } else {
    const char *msg = j ? json_string_value(json_object_get(j, "error")) : NULL;
    ui_toast(msg ? msg : r.err[0] ? r.err : "This access code was not found.", now);
    memset(U.identity_token, 0, sizeof U.identity_token);
  }
  if (j) json_decref(j);
  free(r.body);
}

static void identity_done(const char *code, void *ctx) {
  (void)ctx;
  char esc[200], body[240];
  json_escape(code, esc, sizeof esc);
  snprintf(body, sizeof body, "{\"token\":\"%s\"}", esc);
  snprintf(U.identity_token, sizeof U.identity_token, "%s", code);
  U.identity_job = net_http("POST", "/api/identity", body, false);
  if (!U.identity_job) ui_toast("Connect to tweebbo.lol to restore a character.", now_ms());
}

static void identity_code(void *ctx) {
  (void)ctx;
  osk_open_secret("Private access code from \"Save / restore character\"", 64, identity_done, NULL);
}

static void panel_me(u64 now) {
  Ent *me = world_ent(W.me);
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "My character")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  fill_round(IN_X + 4, IN_Y + 4, 70, 104, 4, sk_rgb(SK_WHITE));
  if (me) {
    bool blink = now % 4000 < 130;
    avatar_draw(scene_look(W.me), 2, POSE_STD, 0, blink ? EXPR_BLINK : 0, 2, 0, false, IN_X + 39 - ANCHOR_X, IN_Y + 100 - ANCHOR_Y, 1, 255);
  }
  float x = IN_X + 82, w = IN_W - 88;
  sk_text(W.name, x, IN_Y + 4, 13, SK_TEXT, 0, w);
  text_draw(W.name, x + 0.6f, IN_Y + 4, 13, sk_rgb(SK_TEXT), 0);
  char line[80];
  snprintf(line, sizeof line, "%u credits", (unsigned)W.credits);
  sk_text(line, x, IN_Y + 22, 10.5f, SK_DIM, 0, w);
  sk_text("Nintendo 3DS player", x, IN_Y + 36, 11, SK_DIM, 0, w - 14);
  console_badge(x + w - 12, IN_Y + 34, 1);
  float bw = (w - 6) / 2, y = IN_Y + 52;
  bool sitting = me && (me->flags & FLAG_SIT), dancing = me && (me->flags & FLAG_DANCE), lying = me && (me->flags & FLAG_LAY);
  if (lying) sk_text("Select the floor to stand up.", x, y + 3, 10, SK_DIM, 0, w);
  else {
    if (sk_button(x, y, bw, "Wave", true)) send_gesture(GESTURE_WAVE);
    if (sk_button(x + bw + 6, y, bw, dancing ? "Stop dancing" : "Dance", true)) send_gesture(GESTURE_DANCE);
    if (sk_button(x, y + 22, bw, sitting ? "Stand up" : "Sit down", true)) send_gesture(GESTURE_SIT);
  }
  if (sk_button(x + bw + 6, y + 22, bw, me && me->drink && now < me->drink_until ? "Put drink away" : "Get a drink", true)) {
    if (me && me->drink && now < me->drink_until) send_drink(0, 0);
    else get_drink(now);
  }
  if (sk_button(x, IN_Y + 92, w/2-3, "Badges", net_online())) profile_open(W.guest_id);
  if (sk_button(x+w/2+3, IN_Y + 92, w/2-3, "Games", net_online())) ui_open(P_GAMES);
  y = IN_Y + 114;
  if (sk_button(IN_X + 4, y, 108, "Change looks", true)) ui_open(P_LOOK);
  if (sk_button(IN_X + 118, y, 80, "Rename", true)) rename_me();
  if (sk_button(IN_X + 204, y, IN_W - 208, "Pets", net_online())) ui_open(P_PETS);
  y += 24;
  // Private code: hidden by default (must not appear in screenshots).
  sk_text("Character code", IN_X + 4, y + 2, 10.5f, SK_TEXT, 0, 92);
  if (net_state() == NET_OFFLINE) sk_text("Only online.", IN_X + 160, y + 2, 10, SK_DIM, 0, 100);
  else {
    if (sk_button(IN_X + 98, y, 66, U.show_code ? "Hide code" : "Show code", net_token()[0] != 0)) U.show_code = !U.show_code;
    if (sk_button(IN_X + 168, y, 66, "Use a code", U.identity_job == 0)) identity_code(NULL);
    if (sk_button(IN_X + 238, y, IN_W - 242, "Move…", true)) ui_open(P_MOVE);
    if (U.show_code && net_token()[0]) {
      fill_round(IN_X + 4, y + 20, IN_W - 8, 15, 3, sk_rgb(SK_WHITE));
      sk_text(net_token(), IN_X + 8, y + 21, 10, SK_TEXT, 0, IN_W - 16);
    }
  }
  identity_poll(now);
}

// ---------------------------------------------------------------- Move panel

static json_t *move_result(int *job, int *status) {
  HttpResult r;
  if (!*job || !net_http_result(*job, &r)) return NULL;
  *job = 0;
  json_error_t err;
  json_t *j = r.body ? json_loads(r.body, 0, &err) : NULL;
  *status = r.status;
  if (!j) j = json_pack("{s:s}", "error", r.err[0] ? r.err : "Could not reach the server.");
  free(r.body);
  return j;
}

static void move_claim(bool confirm) {
  char esc[40], body[96];
  json_escape(U.move_typed, esc, sizeof esc);
  snprintf(body, sizeof body, "{\"code\":\"%s\",\"confirm\":%s}", esc, confirm ? "true" : "false");
  U.move_confirming = confirm;
  U.move_claim_job = net_http("POST", "/api/transfer/claim", body, true);
}

static void move_code_done(const char *code, void *ctx) {
  (void)ctx;
  snprintf(U.move_typed, sizeof U.move_typed, "%s", code);
  move_claim(false);
}

static void move_confirmed(void *ctx) { (void)ctx; move_claim(true); }

static void panel_move(u64 now) {
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Move character")) { ui_open(P_ME); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  float x = IN_X + 6, w = IN_W - 12, y = IN_Y + 4;
  if (net_state() == NET_OFFLINE) { sk_paragraph("Connect to tweebbo.lol to move a character.", x, y, w, 11, SK_TEXT, 3); return; }
  sk_paragraph("Each character can move to another device (PC, phone or console) only once. Afterwards it leaves this device and its old code stops working.",
               x, y, w, 9.5f, SK_DIM, 3);
  y += 38;
  int status = 0;
  json_t *j;
  // This console's character to another device.
  if (sk_button(x, y, w, U.move_job ? "Asking…" : "Get a transfer code", !U.move_job)) U.move_job = net_http("POST", "/api/transfer", "{}", true);
  if ((j = move_result(&U.move_job, &status))) {
    const char *code = json_string_value(json_object_get(j, "code"));
    if (status == 200 && code) {
      snprintf(U.move_code, sizeof U.move_code, "%s", code);
      U.move_expires = now + 10 * 60 * 1000;
    } else ui_toast(json_string_value(json_object_get(j, "error")) ?: "Could not get a code.", now);
    json_decref(j);
  }
  y += 24;
  if (U.move_code[0] && now < U.move_expires) {
    fill_round(x, y, w, 26, 4, sk_rgb(SK_WHITE));
    sk_text(U.move_code, x + w / 2, y + 5, 15, SK_TEXT, 1, w);
    text_draw(U.move_code, x + w / 2 + 0.6f, y + 5, 15, sk_rgb(SK_TEXT), 1);
    char left[48];
    u64 s = (U.move_expires - now) / 1000;
    snprintf(left, sizeof left, "Enter it on the other device. %d:%02d left", (int)(s / 60), (int)(s % 60));
    sk_text(left, x, y + 29, 9.5f, SK_DIM, 0, w);
    // Has the other device taken the character?
    if (!U.move_status_job && now - U.move_polled > 3000) { U.move_polled = now; U.move_status_job = net_http("GET", "/api/transfer", NULL, true); }
    if ((j = move_result(&U.move_status_job, &status))) {
      if (json_is_true(json_object_get(j, "moved"))) {
        U.move_code[0] = 0;
        net_use_token("");
        ui_toast("Your character moved to the other device. This console starts with a new character.", now);
        ui_close();
      }
      json_decref(j);
    }
  } else U.move_code[0] = 0;
  y += 46;
  // Another device's character to this console.
  if (sk_button(x, y, w, U.move_claim_job ? "Checking…" : "Enter a code from another device", !U.move_claim_job)) {
    osk_open("Transfer code from your other device (like K7Q9-M2XP)", NULL, 12, move_code_done, NULL);
  }
  if ((j = move_result(&U.move_claim_job, &status))) {
    const char *name = json_string_value(json_object_get(j, "name")), *token = json_string_value(json_object_get(j, "token"));
    if (status == 200 && U.move_confirming && token) {
      net_use_token(token);
      memset(U.move_typed, 0, sizeof U.move_typed);
      ui_toast("Character moved to this console. Reconnecting…", now);
      ui_close();
    } else if (status == 200 && name) {
      snprintf(U.move_name, sizeof U.move_name, "%s", name);
      char text[160];
      snprintf(text, sizeof text, "Move \"%s\" to this console? Save this console's character code first if you want to keep it.", U.move_name);
      ui_confirm(text, "Move here", move_confirmed, NULL);
    } else ui_toast(json_string_value(json_object_get(j, "error")) ?: "This code is not valid.", now);
    json_decref(j);
  }
}

// ---------------------------------------------------------------- Ent panel (another character)

static void panel_ent(u64 now) {
  Ent *e = world_ent(U.ent);
  if (!e) { ui_close(); return; }
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Character")) { ui_close(); scene_select(0); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  fill_round(IN_X + 4, IN_Y + 4, 70, 104, 4, sk_rgb(SK_WHITE));
  bool blink = (now + e->id * 1777) % 5200 < 130;
  avatar_draw(scene_look(e->id), 2, POSE_STD, 0, blink ? EXPR_BLINK : 0, 2, 0, false, IN_X + 39 - ANCHOR_X, IN_Y + 100 - ANCHOR_Y, 1, 255);
  float x = IN_X + 82, w = IN_W - 88;
  sk_text(e->name, x, IN_Y + 4, 13, SK_TEXT, 0, w);
  text_draw(e->name, x + 0.6f, IN_Y + 4, 13, sk_rgb(SK_TEXT), 0);
  sk_text("In this room", x, IN_Y + 22, 10.5f, SK_DIM, 0, w);
  float y = IN_Y + 42;
  if (e->guest && net_online()) {
    if (sk_button(x, y, w, "Add friend", true)) { friends_act("request", e->guest, ""); ui_toast("Friend request sent.", now); }
    if (sk_button(x + w / 2 + 4, y + 22, w / 2 - 4, "Challenge", true)) games_challenge(e->id);
    bool blocked = friends_is_blocked(e->guest);
    if (sk_button(x, y + 22, w / 2, blocked ? "Unignore" : "Ignore", true)) friends_act(blocked ? "unblock" : "block", e->guest, "");
  }
  if (sk_button(x, IN_Y + 88, w, "Badges", net_online())) profile_open(e->guest);
  if (e->flags & FLAG_3DS) {
    // Same as the web: note that they play from a console.
    console_badge(IN_X + 6, IN_Y + 116, 2);
    sk_paragraph("This person is connected through a Nintendo 3DS system.", IN_X + 36, IN_Y + 118, IN_W - 42, 10.5f, SK_TEXT, 2);
  }
}

// ---------------------------------------------------------------- Look panel (wardrobe)

static int wrap(int v, int n) { return n <= 0 ? 0 : ((v % n) + n) % n; }

typedef struct { const char *label; int *value; int count; const WardrobeEntry *list; const u32 *palette; } Row;

static void panel_look(u64 now) {
  Ent *me = world_ent(W.me);
  if (me && !U.draft_dirty) U.draft = look_decode(me->look);
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Change looks")) { ui_open(P_ME); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  Look *L = &U.draft;
  fill_round(IN_X + 3, IN_Y + 3, 80, 104, 4, sk_rgb(SK_WHITE));
  u64 look = look_encode(L);
  bool blink = now % 4000 < 130;
  avatar_draw(look, U.preview_dir, POSE_STD, 0, blink ? EXPR_BLINK : 0, U.preview_dir, 0, false, IN_X + 43 - ANCHOR_X,
              IN_Y + 100 - ANCHOR_Y, 1, 255);
  if (sk_button(IN_X + 3, IN_Y + 110, 38, "<", true)) U.preview_dir = (U.preview_dir + 1) & 7;
  if (sk_button(IN_X + 45, IN_Y + 110, 38, ">", true)) U.preview_dir = (U.preview_dir + 7) & 7;
  if (sk_button_big(IN_X + 3, IN_Y + IN_H - 27, 80, U.draft_dirty ? "Save" : "Saved", U.draft_dirty)) {
    send_look(look);
    U.draft_dirty = false;
    ui_toast("Look saved.", now);
  }
  Row rows[] = {
    {"Skin", &L->skin, 4, NULL, SKIN},
    {"Hair", &L->hair_style, wardrobe_hair_count, wardrobe_hair, NULL},
    {"Hair color", &L->hair_color, 8, NULL, HAIR},
    {"Top", &L->shirt_style, wardrobe_tops_count, wardrobe_tops, NULL},
    {"Top color", &L->shirt_color, 16, NULL, SHIRT},
    {"Trousers", &L->pants_style, wardrobe_bottoms_count, wardrobe_bottoms, NULL},
    {"Pants color", &L->pants, 8, NULL, PANTS},
    {"Shoes", &L->shoe_style, wardrobe_shoes_count, wardrobe_shoes, NULL},
    {"Shoe color", &L->shoes, 4, NULL, SHOES},
    {"Face", &L->face_style, wardrobe_faces_count, wardrobe_faces, NULL},
  };
  float y = IN_Y + 3, x = IN_X + 88, rw = IN_W - 92;
  int first_row = (U.look_tab / 5) * 5;
  for (int i = first_row; i < first_row + 5 && i < COUNT(rows); i++, y += 26) {
    Row *r = &rows[i];
    bool focus = U.look_tab == i;
    if (focus) fill_round(x - 2, y - 1, rw + 2, 14, 3, sk_rgb(SK_SELECT));
    sk_text(r->label, x, y + 1, 9.5f, SK_TEXT, 0, 66);
    bool prev = sk_button(x + 70, y - 1, 18, "<", true), next = sk_button(x + rw - 18, y - 1, 18, ">", true);
    if (prev || next) { *r->value = wrap(*r->value + (next ? 1 : -1), r->count); U.draft_dirty = true; U.look_tab = i; }
    float cx = x + 92, cw = rw - 114;
    if (r->palette) fill_round(cx, y + 2, cw, 10, 2, sk_rgb(r->palette[*r->value]));
    else sk_text(r->list[wrap(*r->value, r->count)].label, cx + cw / 2, y + 1, 9.5f, SK_TEXT, 1, cw);
  }
  if (sk_button(x, IN_Y + IN_H - 22, 56, "Prev", first_row > 0) || (K.x && first_row > 0)) U.look_tab -= 5;
  sk_text(first_row ? "2 / 2" : "1 / 2", x + rw / 2, IN_Y + IN_H - 20, 11, SK_DIM, 1, 60);
  if (sk_button(x + rw - 56, IN_Y + IN_H - 22, 56, "Next", first_row == 0) || (K.y && first_row == 0)) U.look_tab += 5;
  // D-Pad: up/down picks a row, left/right changes the value.
  if (K.up) U.look_tab = wrap(U.look_tab - 1, COUNT(rows));
  if (K.dn) U.look_tab = wrap(U.look_tab + 1, COUNT(rows));
  if (K.left || K.right) {
    Row *r = &rows[U.look_tab];
    *r->value = wrap(*r->value + (K.right ? 1 : -1), r->count);
    U.draft_dirty = true;
  }
  if (K.a && U.draft_dirty) { send_look(look); U.draft_dirty = false; ui_toast("Look saved.", now); }
}

// ---------------------------------------------------------------- Help panel

static void local_done(const char *url, void *ctx) {
  (void)ctx;
  {
    Endpoint ep;
    if (!endpoint_parse(url, &ep)) { ui_toast("Use ws://host:port or wss://host.", now_ms()); return; }
    if (!net_server_allowed(url)) { ui_toast("Use a private LAN address for this debug build.", now_ms()); return; }
    net_set_server(url);
    ui_close();
  }
}

static void choose_local(void *ctx) {
  (void)ctx;
  osk_open("Server address, e.g. ws://192.168.1.20:8092", strncmp(net_server(), "ws", 2) ? "ws://" : net_server(), 100, local_done, NULL);
}

static void panel_settings(u64 now) {
  (void)now;
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Help & connection")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  float x = IN_X + 6, y = IN_Y + 4, w = IN_W - 12;
  const char *server = net_server();
  NetState st = net_state();
  const char *state = st == NET_ONLINE ? "Online" : st == NET_OFFLINE ? "Offline practice" : st == NET_CONNECTING ? "Connecting…" : st == NET_REPLACED ? "Paused" : "Reconnecting…";
  char line[200];
  snprintf(line, sizeof line, "Server: %s", !strcmp(server, "offline") ? "none (offline)" : server);
  sk_text(line, x, y, 10.5f, SK_TEXT, 0, w);
  snprintf(line, sizeof line, "%s%s%s", state, net_status()[0] ? " · " : "", net_status());
  sk_text(line, x, y + 14, 10, st == NET_ONLINE ? SK_GO : SK_DIM, 0, w);
  sk_text("v" APP_VERSION, x + w, y + 14, 10, SK_DIM, 2, 132);
  y += 32;
#ifndef TWEEBBO_DEBUG
  if (sk_button(x, y, (w - 8) / 3, "tweebbo.lol", strcmp(server, PROD_SERVER) != 0)) { net_set_server(PROD_SERVER); ui_close(); }
#else
  sk_button(x, y, (w - 8) / 3, "Local debug", false);
#endif
  if (sk_button(x + (w - 8) / 3 + 4, y, (w - 8) / 3, "Local server…", true)) choose_local(NULL);
  if (sk_button(x + ((w - 8) / 3 + 4) * 2, y, (w - 8) / 3, "Offline", strcmp(server, "offline") != 0)) { net_set_server("offline"); ui_close(); }
  y += 22;
  static int check_job;
  static char check_text[96];
  if (sk_button(x, y, 120, check_job ? "Checking…" : "Check tweebbo.lol", !check_job && net_state() != NET_OFFLINE)) {
    check_job = net_check_prod();
    snprintf(check_text, sizeof check_text, "Testing the secure connection…");
  }
  HttpResult cr;
  if (check_job && net_http_result(check_job, &cr)) {
    check_job = 0;
    if (cr.status == 200) snprintf(check_text, sizeof check_text, "Secure connection to tweebbo.lol works.");
    else snprintf(check_text, sizeof check_text, "%s", cr.err[0] ? cr.err : "tweebbo.lol did not answer.");
    free(cr.body);
  }
  sk_text(check_text, x + 126, y + 3, 9.5f, SK_DIM, 0, w - 126);
  y += 22;
  // Debug builds update from the PC's LAN download page instead of the public releases.
  UpdateState us = update_state();
  char upd[160];
  if (us == UPD_CHECKING) snprintf(upd, sizeof upd, "Checking for updates…");
  else if (us == UPD_AVAILABLE) snprintf(upd, sizeof upd, "Version %s is available.", update_version());
  else if (us == UPD_DOWNLOADING) snprintf(upd, sizeof upd, "Downloading %d%%…", (int)(update_progress() * 100));
  else if (us == UPD_INSTALLED) snprintf(upd, sizeof upd, "Update installed: restart Tweebbo.");
  else if (us == UPD_FAILED) snprintf(upd, sizeof upd, "%s", update_error());
  else if (us == UPD_LATEST) snprintf(upd, sizeof upd, "You have the latest version (%s).", update_current());
  else snprintf(upd, sizeof upd, "Version %s", update_current());
  if (us == UPD_AVAILABLE || us == UPD_FAILED) {
    if (sk_button(x, y, 120, "Install update", us == UPD_AVAILABLE)) update_start();
  } else if (sk_button(x, y, 120, "Check for updates", us != UPD_CHECKING && us != UPD_DOWNLOADING && net_state() != NET_OFFLINE))
    update_check();
  sk_text(upd, x + 126, y + 3, 9.5f, us == UPD_FAILED ? SK_DANGER : SK_DIM, 0, w - 126);
  y += 24;
  y += sk_paragraph("Move: Circle Pad / D-Pad · Talk/use: A\n"
                    "Wave: X · Dance: Y · Sit/close: B\n"
                    "Overview/rotate: L/R · Quit: START",
                    x, y, w, 9.5f, SK_TEXT, 3) + 3;
  // Plain text, like the web's social links but readable on the console (no links).
  sk_text("Follow Tweebbo", x, y + 2, 9.5f, SK_DIM, 0, w);
  sk_text("tiktok.com/@Tweebbo · instagram.com/Tweebbo", x, y + 14, 9.5f, SK_TEXT, 0, w);
  sk_text("x.com/Tweebbo · youtube.com/@Tweebbo", x, y + 26, 9.5f, SK_TEXT, 0, w);

}

// ---------------------------------------------------------------- frame: header and bar

static void header(void) {
  fill_rect(0, 0, BOT_W, HEAD_H, sk_rgb(0x1f2a30));
  fill_rect(0, HEAD_H - 1, BOT_W, 1, sk_rgb(0x000000));
  sk_text(W.in_room ? W.title : "Tweebbo", 5, 2, 11, SK_WHITE, 0, 150);
  char buf[32];
  snprintf(buf, sizeof buf, "%u", (unsigned)W.credits);
  float x = BOT_W - 5 - text_width(buf, 11);
  text_draw(buf, x, 2, 11, sk_rgb(SK_WHITE), 0);
  // Classic coin stack (credits_icon) instead of a circle.
  sk_sprite("credits_icon@14", x - 16, 2, 1);
  NetState st = net_state();
  if (st == NET_ONLINE) snprintf(buf, sizeof buf, "%d online", W.online);
  else snprintf(buf, sizeof buf, "%s", st == NET_OFFLINE ? "OFFLINE PRACTICE (bots)" : st == NET_CONNECTING ? "connecting…" : st == NET_FULL ? "at capacity" : st == NET_LOCKED ? "locked room" : st == NET_REPLACED || st == NET_DENIED ? "paused" : "reconnecting…");
  x -= 24 + text_width(buf, 10);
  text_draw(buf, x, 3, 10, sk_rgb(st == NET_ONLINE ? 0xd9f2dc : 0xffd34d), 0);
  // Classic messenger head (mes_smallbuddy_head) for connected players.
  if (st == NET_ONLINE) sk_sprite_tint("mes_smallbuddy_head@13", x - 15, 2, 0x8fe39b);
}

typedef struct { Panel panel; const char *label, *icon; } Tool;
static const Tool TOOLS[] = {
  {P_ME, "Me", NULL}, {P_NAV, "Rooms", "building_icon@24"}, {P_SHOP, "Shop", "brochure_icon@24"},
  {P_HAND, "Hand", "hand_icon@26"}, {P_FRIENDS, "Friends", "mes_lite_icon@24"}, {P_CHAT, "Chat", NULL},
  {P_SETTINGS, "Help", "help_icon@24"},
};

static void toolbar(void) {
  // Dark bottom bar (alapalkki_bg) with the original icons.
  int bw, bh;
  if (sk_sprite_size("alapalkki_bg", &bw, &bh))
    for (int x = 0; x < BOT_W; x++) sk_sprite("alapalkki_bg", (float)x, BAR_Y - 2, 1);
  else fill_rect(0, BAR_Y, BOT_W, BAR_H, sk_rgb(SK_BAR));
  fill_rect(0, BAR_Y, BOT_W, 1, sk_rgb(0x000000));
  float w = (float)BOT_W / COUNT(TOOLS);
  Ent *me = world_ent(W.me);
  for (int i = 0; i < COUNT(TOOLS); i++) {
    const Tool *t = &TOOLS[i];
    float cx = i * w + w / 2;
    bool on = U.panel == t->panel || (t->panel == P_ME && (U.panel == P_LOOK || U.panel == P_MOVE));
    if (on) fill_round(i * w + 3, BAR_Y + 2, w - 6, BAR_H - 4, 4, sk_rgba(0xffffff, 40));
    int iw = 0, ih = 0;
    if (t->icon && sk_sprite_size(t->icon, &iw, &ih)) {
      // Pre-shrunk in high quality: crisp at 1:1 on whole pixels.
      sk_sprite(t->icon, floorf(cx - iw / 2.0f), floorf(BAR_Y + 11 - ih / 2.0f), 1);
    } else if (t->panel == P_ME && me) {
      avatar_draw_head(me->look, 3, cx - ANCHOR_X * 0.75f, BAR_Y + 13 - 38 * 0.75f, 0.75f);
    } else if (t->panel == P_CHAT) {
      fill_round(cx - 10, BAR_Y + 5, 20, 13, 6, sk_rgb(0x000000));
      fill_round(cx - 9, BAR_Y + 6, 18, 11, 5, sk_rgb(SK_WHITE));
      C2D_DrawTriangle(cx - 3, BAR_Y + 16, sk_rgb(SK_WHITE), cx + 2, BAR_Y + 16, sk_rgb(SK_WHITE), cx - 4, BAR_Y + 21, sk_rgb(SK_WHITE), 0.5f);
    }
    text_draw(t->label, cx, BAR_Y + 21, 10, sk_rgb(on ? SK_ACCENT : 0xdddddd), 1);
    if (on) fill_rect(i * w + 8, BOT_H - 2, w - 16, 2, sk_rgb(SK_ACCENT));
    if (t->panel == P_FRIENDS && friends_badge() > 0) {
      char n[8];
      snprintf(n, sizeof n, "%d", friends_badge());
      C2D_DrawCircleSolid(cx + 11, BAR_Y + 6, 0.5f, 6, sk_rgb(0xd33a2c));
      text_draw(n, cx + 11, BAR_Y + 1, 8, sk_rgb(SK_WHITE), 1);
    }
    if (sk_hit(i * w, BAR_Y, w, BAR_H)) {
      if (build_active()) build_cancel();
      if (on) ui_close(); else ui_open(t->panel);
    }
  }
}

static bool quit_requested, update_dismissed;

/** Update prompt, download progress and restart. Returns true when it covers the screen. */
static bool update_modal(void) {
  UpdateState st = update_state();
  // A newer build than the one dismissed with Later is offered again.
  static int offer_seen;
  if (update_offer() != offer_seen) { offer_seen = update_offer(); update_dismissed = false; }
  static bool failure_seen;
  if (st != UPD_FAILED) failure_seen = false;
  if (st == UPD_FAILED && !failure_seen && update_version()[0] && strcmp(update_version(), update_current()) &&
      version_cmp(update_version(), APP_VERSION) >= 0) {
    // Install error: explained in a window.
    fill_rect(0, 0, BOT_W, BOT_H, sk_rgba(0x000000, 140));
    float w = 280, h = 120, x = (BOT_W - w) / 2, y = 50;
    bool close = sk_window(x, y, w, h, "Update not installed");
    sk_content(x + 5, y + TITLE_H + 2, w - 10, h - TITLE_H - 7);
    sk_paragraph(update_error(), x + 12, y + TITLE_H + 7, w - 24, 10, SK_TEXT, 5);
    if (close || sk_button(x + w / 2 - 40, y + h - 27, 80, "OK", true) || K.a || K.b) failure_seen = true;
    return true;
  }
  if (!(st == UPD_AVAILABLE && !update_dismissed) && st != UPD_DOWNLOADING && st != UPD_INSTALLED) return false;
  fill_rect(0, 0, BOT_W, BOT_H, sk_rgba(0x000000, 140));
  float w = 280, h = 136, x = (BOT_W - w) / 2, y = 46;
  char title[64];
  snprintf(title, sizeof title, "Tweebbo %s", update_version());
  bool close = sk_window(x, y, w, h, st == UPD_INSTALLED ? "Update installed" : "Update available");
  sk_content(x + 5, y + TITLE_H + 2, w - 10, h - TITLE_H - 7);
  float ix = x + 12, iy = y + TITLE_H + 7, iw = w - 24;
  if (st == UPD_AVAILABLE) {
    char line[96];
    snprintf(line, sizeof line, "Version %s is ready.", update_version());
    sk_text(line, ix, iy, 11, SK_TEXT, 0, iw);
    snprintf(line, sizeof line, "You have %s.", update_current());
    sk_text(line, ix, iy + 15, 10, SK_DIM, 0, iw);
    sk_paragraph(update_notes()[0] ? update_notes() : "Your character and rooms stay the same.", ix, iy + 31, iw, 10, SK_DIM, 3);
    float bw = (iw - 8) / 2;
    if (close || sk_button(ix, y + h - 27, bw, "Later", true) || K.b) update_dismissed = true;
    else if (sk_button(ix + bw + 8, y + h - 27, bw, "Update now", true) || K.a) update_start();
  } else if (st == UPD_DOWNLOADING) {
    sk_text("Downloading and installing…", ix, iy, 11, SK_TEXT, 0, iw);
    float p = update_progress();
    fill_round(ix, iy + 26, iw, 14, 4, sk_rgb(0xcfcfcf));
    fill_round(ix, iy + 26, iw * (p > 1 ? 1 : p), 14, 4, sk_rgb(SK_GO));
    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", (int)(p * 100));
    sk_text(pct, x + w / 2, iy + 27, 10, SK_WHITE, 1, 0);
    sk_paragraph("Keep the console on. You can keep playing; it will ask to restart when it finishes.", ix, iy + 48, iw, 10, SK_DIM, 3);
    return false;
  } else {
    sk_paragraph("The new version is installed. Restart Tweebbo to use it (if it closes instead, open it again from the HOME Menu). Your character and rooms stay the same.", ix, iy, iw, 10.5f,
                 SK_TEXT, 4);
    if (sk_button(ix + iw / 2 - 60, y + h - 27, 120, "Restart now", true) || K.a) quit_requested = update_restart();
  }
  return true;
}

static bool update_modal_up;

static void modal(void) {
  update_modal_up = update_modal();
  if (update_modal_up) return;
  if (!U.alert && !U.confirm) return;
  fill_rect(0, 0, BOT_W, BOT_H, sk_rgba(0x000000, 120));
  float w = 270, h = 118, x = (BOT_W - w) / 2, y = 56;
  if (U.alert) {
    // Hotel alert: nearly the whole screen, so long messages fit.
    float aw = 300, ah = 200, ax = (BOT_W - aw) / 2, ay = 20;
    char title[120];
    if (U.alert_n > 1) snprintf(title, sizeof title, "%s (1/%d)", U.alerts[0].title, U.alert_n);
    else snprintf(title, sizeof title, "%s", U.alerts[0].title);
    bool close = sk_window(ax, ay, aw, ah, title);
    sk_content(ax + 5, ay + TITLE_H + 2, aw - 10, ah - TITLE_H - 7);
    bool illustrated = U.alerts[0].capacity;
    if (illustrated) sk_sprite("frank-maintenance", ax + 12, ay + TITLE_H + 8, 1);
    int next = sk_paragraph_page(U.alerts[0].text, U.alert_offsets[U.alert_page], ax + (illustrated ? 78 : 11),
      ay + TITLE_H + 6, aw - (illustrated ? 90 : 22), 11, SK_TEXT, 9);
    bool more = U.alerts[0].text[next] != 0 && U.alert_page < 63;
    if (sk_button(ax + 10, ay + ah - 28, 75, "Previous", U.alert_page > 0) || (K.left && U.alert_page > 0)) U.alert_page--;
    if (sk_button(ax + aw - 85, ay + ah - 28, 75, "Next", more) || (K.right && more)) U.alert_offsets[++U.alert_page] = next;
    if (close || sk_button(ax + aw / 2 - 40, ay + ah - 28, 80, "OK", true) || K.a || K.b) {
      if (U.seen_n == COUNT(U.seen_alerts)) { memmove(U.seen_alerts, U.seen_alerts + 1, sizeof(int) * (COUNT(U.seen_alerts) - 1)); U.seen_n--; }
      U.seen_alerts[U.seen_n++] = U.alerts[0].id;
      memmove(&U.alerts[0], &U.alerts[1], sizeof(U.alerts[0]) * (size_t)(COUNT(U.alerts) - 1));
      U.alert_page = 0; U.alert_offsets[0] = 0;
      U.alert_n--;
      U.alert = U.alert_n > 0;
    }
    return;
  }
  bool close = sk_window(x, y, w, h, "Confirm");
  sk_content(x + 5, y + TITLE_H + 2, w - 10, h - TITLE_H - 7);
  sk_paragraph(U.confirm_text, x + 11, y + TITLE_H + 6, w - 22, 10.5f, SK_TEXT, 4);
  float bw = (w - 30) / 2;
  if (close || sk_button(x + 10, y + h - 28, bw, "Cancel", true) || K.b) { U.confirm = false; return; }
  if (sk_button(x + 20 + bw, y + h - 28, bw, U.confirm_yes, true) || K.a) {
    U.confirm = false;
    if (U.confirm_fn) U.confirm_fn(U.confirm_ctx);
  }
}

// ---------------------------------------------------------------- input and drawing

static u32 down_keys, held_keys;

bool ui_input(u32 down, u32 held, u64 now) {
  if (down & KEY_START || quit_requested) return false;
  scene_prompt(NULL);
  // Stylus: touch, drag, and a "click" when lifted without dragging.
  touchPosition tp;
  hidTouchRead(&tp);
  bool touching = held & KEY_TOUCH;
  T.tap = down & KEY_TOUCH;
  T.released = false;
  T.dy = 0;
  if (T.tap) { T.x0 = T.x = tp.px; T.y0 = T.y = tp.py; T.dragging = false; }
  if (touching && !T.tap) {
    T.dy = tp.py - T.y;
    T.x = tp.px; T.y = tp.py;
    if (abs(T.x - T.x0) > 6 || abs(T.y - T.y0) > 6) T.dragging = true;
  }
  if (!touching && T.held) T.released = !T.dragging;
  T.held = touching;
  down_keys = down;
  held_keys = held;

  UpdateState us = update_state();
  bool blocking_update = (us == UPD_AVAILABLE && !update_dismissed) || us == UPD_INSTALLED || update_modal_up;
  bool window = U.panel != P_ROOM || U.alert || U.confirm || blocking_update;
  memset(&K, 0, sizeof K);
  K.down = down; K.held = held;
  if (window) {
    K.up = down & KEY_DUP; K.dn = down & KEY_DDOWN; K.left = down & KEY_DLEFT; K.right = down & KEY_DRIGHT;
    K.a = down & KEY_A; K.b = down & KEY_B; K.x = down & KEY_X; K.y = down & KEY_Y;
  }
  if (osk_active()) {
    osk_input(down, held, now);
    // The Circle Pad still walks while typing.
    if (W.in_room && U.panel != P_SNOW && U.panel != P_CASINO && U.panel != P_GAME) stick_walk(held, false, now);
    return true;
  }
  if (net_state() == NET_LOCKED || net_state() == NET_FULL || net_state() == NET_DENIED) return true;
  if (net_state() == NET_REPLACED) {
    if (down & KEY_A) net_resume();
    return true;
  }
  if (!W.in_room) return true;
  if (U.alert || U.confirm || blocking_update) return true;
  if (U.panel == P_SNOW) { snow_input(down, held, now); return true; }
  if (build_input(down, held, now)) return true;
  if (down & KEY_SELECT) {
    static const Panel cycle[] = {P_ROOM, P_ME, P_NAV, P_SHOP, P_HAND, P_FRIENDS, P_CHAT};
    int i = 0;
    while (i < COUNT(cycle) && cycle[i] != U.panel) i++;
    ui_open(cycle[(i + 1) % COUNT(cycle)]);
  }
  if (down & (KEY_L | KEY_R)) scene_toggle_overview();
  if (!window) {
    Context ctx;
    bool near = context_find(&ctx, now);
    scene_prompt(near ? ctx.label : NULL);
    if (down & KEY_A) { if (near) context_run(&ctx, now); else talk(); }
    if (down & KEY_X) send_gesture(GESTURE_WAVE);
    if (down & KEY_Y) send_gesture(GESTURE_DANCE);
    if (down & KEY_B) send_gesture(GESTURE_SIT);
  } else if (K.b && U.panel != P_LOOK && U.panel != P_GAME) {
    // B closes the window (the wardrobe returns to Me from its own panel).
    if (U.panel == P_ENT) scene_select(0);
    ui_close();
    K.b = false;
  } else if (K.b && U.panel == P_LOOK) ui_open(P_ME);
  // The Circle Pad always walks; the D-Pad only when no window is open.
  if (U.panel != P_CASINO && U.panel != P_GAME) stick_walk(held, !window, now);
  return true;
}

void ui_draw(u64 now) {
  fill_rect(0, 0, BOT_W, BOT_H, sk_rgb(0x111111));
  friends_poll(now);
  games_poll();
  casino_poll(now);
  if (osk_draw(now)) return;
  NetState blocked = net_state();
  if (blocked == NET_LOCKED || blocked == NET_FULL || blocked == NET_DENIED) {
    header();
    sk_status_window(10, 28, 300, 198, blocked == NET_LOCKED ? "Locked room" : blocked == NET_FULL ? "Over capacity" : "Connection paused");
    sk_content(15, 49, 290, 172);
    sk_sprite("frank-maintenance", 25, 62, 1);
    sk_paragraph(net_status(), 94, 62, 200, 11, SK_TEXT, 5);
    if (blocked == NET_FULL) {
      sk_paragraph("Soon you can help Tweebbo grow.", 94, 138, 200, 10, SK_DIM, 2);
      sk_button(94, 172, 93, "Donate", false);
      sk_button(194, 172, 100, "Sponsor", false);
    }
    if (blocked == NET_LOCKED) {
      if (sk_button(94, 153, 200, "Enter password", true) || (down_keys & KEY_A)) {
        osk_open_secret("Room password", 32, unlock_done, NULL);
      }
    } else {
      int seconds = net_retry_seconds();
      char label[40];
      snprintf(label, sizeof label, seconds && blocked == NET_FULL ? "Retry in %d s" : "Reconnect", seconds);
      if (sk_button(94, 199, 200, label, blocked != NET_FULL || !seconds)) net_resume();
    }
    if (blocked == NET_LOCKED || net_room_full()) {
      if (sk_button(24, 199, 65, "Lobby", true) || (down_keys & KEY_B)) net_goto("lobby", "");
    }
    return;
  }
  if (net_state() == NET_REPLACED) {
    // Same character on another device: wait until the player takes it back.
    header();
    sk_status_window(20, 60, BOT_W - 40, 90, "Tweebbo");
    sk_content(25, 79, BOT_W - 50, 66);
    sk_paragraph("Tweebbo is open on another device or browser.", 34, 92, BOT_W - 68, 11, SK_TEXT, 3);
    if (sk_button(BOT_W / 2 - 60, 160, 120, "Play here (A)", true)) net_resume();
    return;
  }
  if (!W.in_room) {
    header();
    sk_status_window(20, 60, BOT_W - 40, 90, "Tweebbo");
    sk_content(25, 79, BOT_W - 50, 66);
    const char *s = net_status();
    char where[160];
    snprintf(where, sizeof where, "Connecting to %s…", net_server());
    sk_text(net_state() == NET_OFFLINE ? "Loading…" : where, BOT_W / 2, 88, 11, SK_TEXT, 1, BOT_W - 60);
    sk_text("v" APP_VERSION, BOT_W - 26, 132, 9, SK_DIM, 2, 60);
    if (s[0]) sk_paragraph(s, 34, 108, BOT_W - 68, 10, SK_DIM, 3);
    if (sk_button(BOT_W / 2 - 60, 160, 120, "Settings", true)) ui_open(P_SETTINGS);
    if (U.panel == P_SETTINGS) { fill_rect(0, 0, BOT_W, BOT_H, sk_rgb(0x111111)); header(); panel_settings(now); }
    toolbar();
    U.panel = U.panel == P_SETTINGS ? P_SETTINGS : P_ROOM;
    return;
  }
  Touch saved_touch = T;
  Keys saved_keys = K;
  confirm_opened = false;
  bool had_modal = U.alert || U.confirm || update_modal_up;
  if (had_modal) { memset(&T, 0, sizeof T); memset(&K, 0, sizeof K); }
  if (build_active()) build_draw_panel(now);
  else switch (U.panel) {
    case P_CASINO: casino_draw(now); break;
    case P_SNOW: snow_draw(now); break;
    case P_PROFILE: profile_draw(now); break;
    case P_GAME: games_draw(now); break;
    case P_GAMES: games_hub_draw(now); break;
    case P_ROOM: panel_room(now); break;
    case P_NAV: nav_draw(now); break;
    case P_SHOP: shop_draw(now); break;
    case P_HAND: hand_draw(now); break;
    case P_ME: panel_me(now); break;
    case P_MOVE: panel_move(now); break;
    case P_FRIENDS: friends_draw(now); break;
    case P_CHAT: panel_chat(now); break;
    case P_SETTINGS: panel_settings(now); break;
    case P_LOOK: panel_look(now); break;
    case P_ITEM: item_draw(now); break;
    case P_ENT: panel_ent(now); break;
    case P_PETS: pets_draw(now); break;
    default: panel_room(now); break;
  }
  header();
  toolbar();
  if (now < U.toast_until) {
    char t[160];
    snprintf(t, sizeof t, "%s", U.toast);
    text_fit(t, 10.5f, BOT_W - 24);
    float w = text_width(t, 10.5f) + 16;
    fill_round((BOT_W - w) / 2 - 1, HEAD_H + 3, w + 2, 19, 5, sk_rgb(0x000000));
    fill_round((BOT_W - w) / 2, HEAD_H + 4, w, 17, 4, sk_rgb(0xf2e6a5));
    text_draw(t, BOT_W / 2, HEAD_H + 6, 10.5f, sk_rgb(0x1b1b1b), 1);
  }
  if (had_modal) { T = saved_touch; K = saved_keys; }
  else if (U.alert || U.confirm || update_modal_up) { memset(&T, 0, sizeof T); memset(&K, 0, sizeof K); }
  // A dialog opened during this frame (by a panel) waits for the next press.
  if (confirm_opened) { memset(&K, 0, sizeof K); T.released = false; }
  modal();
  T = saved_touch; K = saved_keys;
}

u32 ui_item_id(void) { return U.item; }
