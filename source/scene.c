#include "scene.h"
#include "igloo.h"
#include "avatar.h"
#include <jansson.h>
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "pets.h"
#include "pool.h"
#include "bar.h"
#include "proto.h"
#include "world.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TW 64
#define TH 32
#define HALF_W 32
#define HALF_H 16

#define BUBBLE_H 19
#define BUBBLE_ROW 21
#define BUBBLE_MAX 7
#define BUBBLE_SCROLL_MS 4000
#define BUBBLE_SPEED 0.18f
#define BUBBLE_BASE 74
#define BUBBLE_TEXT 12.0f

typedef struct {
  bool used;
  char name[24], text[104];
  u32 color;
  float wx, head_wy, y;
  int row;
  u64 t0;
} Bubble;

static struct {
  float cam_x, cam_y, zoom;
  bool overview, snapped;
  const RoomBackground *bg;
  const Scene *scene;
  float bx0, by0, bx1, by1;
  Bubble chat[BUBBLE_MAX];
  u64 last_shift, last_frame, joined_at;
  int dest_x, dest_y;
  u64 dest_until;
  u32 selected;
} V = {.zoom = 1};

static json_t *owner_snapshot;
static u32 owner_revision;

static void iso(float x, float y, float *sx, float *sy) {
  *sx = (x - y) * HALF_W;
  *sy = (x + y) * HALF_H;
}

static u32 rgba(u32 rgb, u8 a) { return C2D_Color32(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, a); }

void scene_select(u32 eid) { V.selected = eid; }
u32 scene_selected(void) { return V.selected; }
bool scene_overview(void) { return V.overview; }

bool scene_water_tile(int x, int y) {
  if (!V.scene || V.scene->water < 0 || x < 0 || y < 0 || x >= W.w || y >= W.h) return false;
  char c = W.rows[y * W.w + x];
  return c >= '0' && c <= '9' && c - '0' <= V.scene->water;
}

/** Overview zoom: always exactly 0.5x. A whole-number ratio keeps the pixel art intact: with
    integer screen positions every pixel is the plain average of a 2x2 block of the original,
    while fractional scales (the old fit-to-screen 0.2-0.5x) sampled scattered texels and broke
    outlines. Rooms larger than the 0.5x view scroll with the player instead of shrinking. */
static float overview_zoom(void) { return 0.5f; }

void scene_toggle_overview(void) {
  V.overview = !V.overview;
  V.zoom = V.overview ? overview_zoom() : 1;
  V.snapped = false;
  gfx_filter(V.overview);
}

void scene_mark_destination(int x, int y, u64 now) {
  if (!world_tile(x, y)) { V.dest_until = 0; return; }
  V.dest_x = x; V.dest_y = y; V.dest_until = now + 15000;
}

static void draw_prompt(void);

void scene_room_changed(u64 now) {
  owner_revision = W.owners_rev - 1;
  // A connection renewal sends ROOM again: only clear the bubbles when the room changes.
  static char last_slug[sizeof W.slug];
  if (strcmp(last_slug, W.slug)) memset(V.chat, 0, sizeof V.chat);
  snprintf(last_slug, sizeof last_slug, "%s", W.slug);
  // Park and pool: their own layers. Everything else: classic background or exported floor.
  V.scene = gfx_scene(W.model);
  V.bg = V.scene ? NULL : gfx_room(W.model);
  if (V.scene) gfx_room_release();
  if (!V.bg && !V.scene) {
    // Public models that reuse a classic background of the same shape (renderer.ts).
    for (int i = 0; i < room_background_count && !V.bg; i++) {
      const RoomBackground *bg = &room_backgrounds[i];
      if (bg->door_x != W.door_x || bg->door_y != W.door_y) continue;
      bool same = true;
      const char *p = bg->rows;
      for (int y = 0; y < W.h && same; y++) {
        for (int x = 0; x < W.w; x++, p++) if (!*p || *p == '\n' || ((*p != 'x') != world_tile(x, y))) { same = false; break; }
        if (*p == '\n') p++; else if (*p) same = false;
      }
      if (same) V.bg = gfx_room(bg->model);
    }
  }
  if (V.scene) {
    for (int i = 0; i < V.scene->count; i++) {
      const SceneSprite *sp = &V.scene->sprites[i];
      // The floor sets the bounds; the sky under a rooftop is wider than the room.
      if (!sp->background || sp->role == 1) continue;
      V.bx0 = sp->x; V.by0 = sp->y; V.bx1 = sp->x + sp->dw; V.by1 = sp->y + sp->dh;
    }
  } else if (V.bg) {
    V.bx0 = V.bg->x; V.by0 = V.bg->y; V.bx1 = V.bg->x + V.bg->w; V.by1 = V.bg->y + V.bg->h;
  } else {
    V.bx0 = V.by0 = 1e9f; V.bx1 = V.by1 = -1e9f;
    for (int y = 0; y < W.h; y++)
      for (int x = 0; x < W.w; x++) {
        if (!world_tile(x, y)) continue;
        float sx, sy;
        iso(x, y, &sx, &sy);
        V.bx0 = fminf(V.bx0, sx - HALF_W - 14); V.bx1 = fmaxf(V.bx1, sx + HALF_W + 14);
        V.by0 = fminf(V.by0, sy - 130); V.by1 = fmaxf(V.by1, sy + TH + 10);
      }
  }
  for (int i = 0; i < MAX_ITEMS; i++) if (W.items[i].used) gfx_furni_preload(W.items[i].def);
  if (V.overview) V.zoom = overview_zoom();
  V.snapped = false;
  V.joined_at = now;
  V.dest_until = 0;
  V.selected = 0;
}

// ---------------------------------------------------------------- camera

static void camera(u64 now) {
  float lw = TOP_W / V.zoom, lh = TOP_H / V.zoom, tx, ty;
  Ent *me = world_ent(W.me);
  if (G.on && !V.overview) {
    // While building, the camera follows the ghost furniture.
    float sx, sy;
    iso(G.x + .5f, G.y + .5f, &sx, &sy);
    tx = lw * .5f - sx;
    ty = lh * .6f - sy;
    if (V.bx1 - V.bx0 >= lw) tx = fminf(-V.bx0, fmaxf(lw - V.bx1, tx)); else tx = lw / 2 - (V.bx0 + V.bx1) / 2;
    if (V.by1 - V.by0 >= lh) ty = fminf(-V.by0, fmaxf(lh - V.by1, ty)); else ty = lh / 2 - (V.by0 + V.by1) / 2;
  } else if (!V.overview && pool_camera(&tx, &ty, now)) {
    // Wobble Squabble players see the whole pole; a landing jumper sees the pool.
    tx = lw * .5f - tx;
    ty = lh * .6f - ty;
    if (V.bx1 - V.bx0 >= lw) tx = fminf(-V.bx0, fmaxf(lw - V.bx1, tx)); else tx = lw / 2 - (V.bx0 + V.bx1) / 2;
    if (V.by1 - V.by0 >= lh) ty = fminf(-V.by0, fmaxf(lh - V.by1, ty)); else ty = lh / 2 - (V.by0 + V.by1) / 2;
  } else if (!me) {
    tx = lw / 2 - (V.bx0 + V.bx1) / 2;
    ty = lh / 2 - (V.by0 + V.by1) / 2;
  } else {
    float px, py, sx, sy;
    ent_pos(me, now, &px, &py);
    iso(px, py, &sx, &sy);
    tx = lw * .5f - sx;
    ty = lh * .68f - sy + ent_height(me, now);
    // No extra black when the room is larger than the screen.
    if (V.bx1 - V.bx0 >= lw) tx = fminf(-V.bx0, fmaxf(lw - V.bx1, tx));
    else tx = lw / 2 - (V.bx0 + V.bx1) / 2;
    if (V.by1 - V.by0 >= lh) ty = fminf(-V.by0, fmaxf(lh - V.by1, ty));
    else ty = lh / 2 - (V.by0 + V.by1) / 2;
  }
  if (!V.snapped) { V.cam_x = tx; V.cam_y = ty; V.snapped = true; }
  V.cam_x += (tx - V.cam_x) * .12f;
  V.cam_y += (ty - V.cam_y) * .12f;
}

// In the overview everything lands on whole screen pixels (see overview_zoom).
static float scr_x(float wx) { return V.overview ? roundf((V.cam_x + wx) * V.zoom) : roundf(V.cam_x * V.zoom) + wx * V.zoom; }
static float scr_y(float wy) { return V.overview ? roundf((V.cam_y + wy) * V.zoom) : roundf(V.cam_y * V.zoom) + wy * V.zoom; }

// ---------------------------------------------------------------- poses

typedef struct { Pose pose; int frame; float lift, dx, dy, depth, anchor_y; } PoseInfo;

static bool in_water(const Ent *e) { return scene_water_tile(e->x, e->y); }

static PoseInfo pose_of(const Ent *e, u64 now, bool moving) {
  PoseInfo p = {POSE_STD, 0, ent_height(e, now), 0, 0, 0, ANCHOR_Y};
  if (in_water(e)) {
    static const int BOB[6] = {0, 1, 2, 3, 2, 1};
    p.pose = moving ? POSE_SWIM : POSE_FLOAT;
    p.frame = BOB[(now / 160) % 6];
    return p;
  }
  if ((e->flags & FLAG_LAY) && (!moving || (e->flags & FLAG_ROLL))) {
    Item *bed = world_item_at(e->x, e->y);
    const FurniDef *d = bed ? def_get(bed->def) : NULL;
    float height = bed ? (d ? d->seat : 32) + bed->z : 32;
    p.pose = POSE_LAY;
    p.lift = height - 32 - (bed ? bed->z : 0) + ent_height(e, now);
    p.dx = e->dir == 0 ? -47 : 10; p.dy = e->dir == 0 ? 32 : 30; p.depth = 2; p.anchor_y = 62;
    return p;
  }
  if ((e->flags & FLAG_SIT) && (!moving || (e->flags & FLAG_ROLL))) {
    Item *seat = world_item_at(e->x, e->y);
    const FurniDef *d = seat ? def_get(seat->def) : NULL;
    if (d && d->kind == KIND_SIT) { p.pose = POSE_SIT; p.lift = d->seat - SEATED_HIP + ent_height(e, now); }
    else p.pose = POSE_FLOOR;
    return p;
  }
  if (moving && (e->flags & FLAG_ROLL)) return p;
  if (moving) { p.pose = POSE_WALK; p.frame = (int)(now / 125) & 3; }
  else if (e->flags & FLAG_WAVE) { p.pose = POSE_WAVE; p.frame = (int)(now / 220) & 1; }
  else if (e->flags & FLAG_DANCE) { p.pose = POSE_DANCE; p.frame = (int)(now / 160) & 7; }
  return p;
}

static int angle_dir(float dx, float dy) {
  return ((int)lroundf(atan2f(dx, -dy) / ((float)M_PI / 4)) + 8) & 7;
}

static int body_dir(const Ent *e, bool moving) {
  if (!moving || (e->flags & FLAG_ROLL)) return e->dir;
  if (e->move_dir >= 0) return e->move_dir;
  return angle_dir(e->x - e->fx, e->y - e->fy);
}

/** Turns the head toward the nearest avatar when it is one turn step away. */
static int head_dir(const Ent *e, u64 now, bool moving) {
  if (moving) return body_dir(e, true);
  if (e->flags & (FLAG_WAVE | FLAG_DANCE)) return e->dir;
  float ex, ey, best = 9, tx = 0, ty = 0;
  bool found = false;
  ent_pos(e, now, &ex, &ey);
  for (int i = 0; i < MAX_ENTS; i++) {
    const Ent *o = &W.ents[i];
    if (!o->used || o->id == e->id) continue;
    float x, y;
    ent_pos(o, now, &x, &y);
    float d = (x - ex) * (x - ex) + (y - ey) * (y - ey);
    if (d > .1f && d < best) { best = d; tx = x; ty = y; found = true; }
  }
  if (!found) return e->dir;
  float dx = tx - ex, dy = ty - ey;
  if (hypotf(dx, dy) < .4f) return e->dir;
  int facing = angle_dir(dx, dy), turn = (facing - e->dir + 8) & 7;
  return turn == 1 || turn == 7 ? facing : e->dir;
}

// ---------------------------------------------------------------- drawing

typedef struct { float depth; int seq; const Item *item; const FurniPart *part; const Ent *ent; const SceneSprite *layer; const Pet *pet; } Drawable;

static int by_depth(const void *a, const void *b) {
  const Drawable *x = a, *y = b;
  if (x->depth != y->depth) return x->depth < y->depth ? -1 : 1;
  return x->seq - y->seq;
}

static void draw_item_part(const Item *it, const FurniPart *p) {
  float sx, sy;
  iso(it->x, it->y, &sx, &sy);
  bool add = p->flags & PART_ADD;
  u8 alpha = it->id == 0 && G.on ? (u8)(p->alpha * 7 / 10) : p->alpha;
  if (add) gfx_additive(true);
  gfx_furni(it->def, p, scr_x(sx + p->ox), scr_y(sy + p->oy - it->z), V.zoom, C2D_Color32(255, 255, 255, alpha));
  if (add) gfx_additive(false);
}

// The owner snapshot (founders on show and character skins) is JSON; it is parsed into these
// small tables once per revision instead of walking the JSON for every avatar on every frame.
#define MAX_OWNERS 16
#define MAX_SKINS 64
static struct { u32 guest; bool presence; } owners[MAX_OWNERS];
static struct { u32 guest; u8 skin; } skins[MAX_SKINS];
static int owner_n, skin_n;

static void owners_refresh(void) {
  if (owner_revision == W.owners_rev) return;
  json_decref(owner_snapshot);
  owner_snapshot = W.owners_json ? json_loads(W.owners_json, 0, NULL) : NULL;
  owner_revision = W.owners_rev;
  owner_n = skin_n = 0;
  json_t *entry;
  size_t i;
  json_array_foreach(json_object_get(owner_snapshot, "visible"), i, entry)
    if (owner_n < MAX_OWNERS) {
      owners[owner_n].guest = (u32)json_integer_value(json_object_get(entry, "guest"));
      owners[owner_n++].presence = json_is_true(json_object_get(entry, "presence"));
    }
  json_array_foreach(json_object_get(owner_snapshot, "skins"), i, entry) {
    int skin = (int)json_integer_value(json_object_get(entry, "skin"));
    if (skin_n < MAX_SKINS && skin >= 1 && skin <= 7)
      skins[skin_n].guest = (u32)json_integer_value(json_object_get(entry, "guest")), skins[skin_n++].skin = (u8)skin;
  }
}

/** The founder entry of this guest (-1 when they are not a founder on show). */
static int owner_index(u32 guest) {
  owners_refresh();
  for (int i = 0; i < owner_n; i++) if (owners[i].guest == guest) return i;
  return -1;
}

static u64 appearance(const Ent *e) {
  bool founder = owner_index(e->guest) >= 0;
  for (int i = 0; i < skin_n; i++)
    if (skins[i].guest == e->guest) return e->look | ((u64)skins[i].skin << 48);
  return pool_look(e, e->look) | (founder ? AV_FOUNDER : (e->flags & 128) ? AV_FRANK : 0);
}

static void founder_diamond(float x, float y, float width, u8 alpha) {
  u32 gold = C2D_Color32(0xe9, 0xc4, 0x63, alpha);
  float dx = width * V.zoom, dy = width * .48f * V.zoom;
  C2D_DrawLine(x, y - dy, gold, x + dx, y, gold, 1, .5f);
  C2D_DrawLine(x + dx, y, gold, x, y + dy, gold, 1, .5f);
  C2D_DrawLine(x, y + dy, gold, x - dx, y, gold, 1, .5f);
  C2D_DrawLine(x - dx, y, gold, x, y - dy, gold, 1, .5f);
}

u64 scene_look(u32 eid) { Ent *e = world_ent(eid); return e ? appearance(e) : 0; }

static void draw_ent(const Ent *e, u64 now) {
  float px, py, sx, sy;
  ent_pos(e, now, &px, &py);
  iso(px, py, &sx, &sy);
  bool moving = ent_moving(e, now);
  PoseInfo p = pose_of(e, now, moving);
  float h = ent_height(e, now);
  if (igloo_draw(e, body_dir(e, moving), p.pose, now, scr_x(roundf(sx)), scr_y(roundf(sy + HALF_H) - h), V.zoom)) return;
  bool swim = p.pose == POSE_SWIM || p.pose == POSE_FLOAT;
  if (swim) {
    bool talking = now < e->talk_until && (now / 140) % 2 == 0;
    bool blink = (now + e->id * 1777) % 5200 < 130;
    avatar_draw_swim(appearance(e), body_dir(e, moving), p.pose, p.frame, (talking ? EXPR_TALK : 0) | (blink ? EXPR_BLINK : 0),
                     head_dir(e, now, moving), scr_x(roundf(sx) - SWIM_ANCHOR_X), scr_y(roundf(sy + HALF_H) - p.anchor_y - p.lift), V.zoom);
    return;
  }
  if (p.pose != POSE_LAY)
    C2D_DrawEllipseSolid(scr_x(roundf(sx) - 14), scr_y(roundf(sy + HALF_H - h) - 7), 0.5f, 28 * V.zoom, 14 * V.zoom,
                         C2D_Color32(0, 0, 0, 64));
  static u32 arrival_id[MAX_ENTS];
  static u64 arrival_at[MAX_ENTS];
  int slot = (int)(e - W.ents);
  int owner = owner_index(e->guest);
  bool presence = owner >= 0 && owners[owner].presence;
  if (slot >= 0 && slot < MAX_ENTS && presence && p.pose != POSE_LAY) {
    if (arrival_id[slot] != e->id) { arrival_id[slot] = e->id; arrival_at[slot] = now; }
    u64 age = now - arrival_at[slot];
    float cx = scr_x(sx), cy = scr_y(sy + HALF_H - h);
    if (age < 1000) founder_diamond(cx, cy, 25 + 12 * age / 1000.f, (u8)(255 * (1000 - age) / 1000));
    founder_diamond(cx, cy, 25, 255);
    static const int corners[4][2] = {{0,-12},{25,0},{0,12},{-25,0}};
    for (int i = 0; i < 4; i++) fill_rect(roundf(cx + corners[i][0] * V.zoom) - 1, roundf(cy + corners[i][1] * V.zoom) - 1, 3, 3, rgba(0xffe7a2, 255));
  } else if (slot >= 0 && slot < MAX_ENTS) arrival_id[slot] = 0;
  bool talking = now < e->talk_until && (now / 140) % 2 == 0;
  bool blink = (now + e->id * 1777) % 5200 < 130;
  int drink = now < e->drink_until ? e->drink : 0;
  int bar = bar_sip(e, now, moving);
  bool sip = bar >= 0 ? bar == 1 : !moving && (now + e->id * 177) % 6000 < 900;
  avatar_draw(appearance(e), body_dir(e, moving), p.pose, p.frame, (talking ? EXPR_TALK : 0) | (blink ? EXPR_BLINK : 0),
              head_dir(e, now, moving), drink, sip,
              scr_x(roundf(sx) + p.dx - ANCHOR_X), scr_y(roundf(sy + HALF_H) + p.dy - p.anchor_y - p.lift), V.zoom, 255);
}

static void tile_cursor(int x, int y, u32 color) {
  float sx, sy;
  iso(x, y, &sx, &sy);
  float cx = scr_x(sx), top = scr_y(sy), z = V.zoom;
  float l = cx - HALF_W * z, r = cx + HALF_W * z, mid = top + HALF_H * z, bot = top + TH * z;
  C2D_DrawLine(cx, top, color, r, mid, color, 1, 0.5f);
  C2D_DrawLine(r, mid, color, cx, bot, color, 1, 0.5f);
  C2D_DrawLine(cx, bot, color, l, mid, color, 1, 0.5f);
  C2D_DrawLine(l, mid, color, cx, top, color, 1, 0.5f);
}

/** Pixel-art handheld of each native console; same arts as client/src/console-badge.ts. */
static const char *const BADGE_3DS[] = {
  ".KKKKKKKKKK.", "K##########K", "K#SSSSSSSS#K", "K#SLSSSSSS#K", "K#SSSSSSSS#K", "K##########K", ".KHHHHHHHHK.",
  "K##########K", "K#c#ssss#b#K", "Kccc#ss#b#bK", "K#c#ssss#b#K", "K##########K", ".KKKKKKKKKK.",
};
static const char *const BADGE_SWITCH[] = {
  ".KKKKKKKKKKKK.", "KBBKSSSSSSKRRK", "KBcKSLSSSSKRbK", "KBBKSSSSSSKbRK", "KBbKSSSSSSKRRK",
  "KBBKSSSSSSKRcK", "KBBKSSSSSSKRRK", "KBBKKKKKKKKRRK", ".KKKKKKKKKKKK.",
};
static const char *const BADGE_VITA[] = {
  ".KKKKKKKKKK.", "KWWKBBBBKWWK", "KWcKBBBBKcWK", "KWWKBBBBKWWK", "KwWKBLLBKWWK",
  "KWWKBBBBKWWK", "KWWKBBBBKWWK", "KWWKKKKKKWWK", ".KKKKKKKKKK.",
};
typedef struct { char ch; u8 r, g, b; } BadgeColor;
static const BadgeColor PAL_3DS[] = {{'K',0x14,0x18,0x1c},{'#',0x3f,0xa6,0xe0},{'H',0x23,0x66,0x8f},{'S',0x8f,0xd9,0xff},{'L',0xe8,0xf8,0xff},{'s',0xd7,0xee,0xf8},{'c',0x2b,0x31,0x38},{'b',0x2b,0x31,0x38}};
static const BadgeColor PAL_SWITCH[] = {{'K',0x14,0x18,0x1c},{'B',0x00,0xc3,0xe3},{'R',0xff,0x45,0x54},{'S',0x23,0x2a,0x30},{'L',0x8f,0xd9,0xff},{'c',0x14,0x18,0x1c},{'b',0xf6,0xf6,0xef}};
static const BadgeColor PAL_VITA[] = {{'K',0x14,0x18,0x1c},{'W',0x2c,0x35,0x3d},{'B',0x1b,0x5b,0x91},{'L',0x2d,0x7f,0xc4},{'c',0x0f,0x14,0x17},{'w',0x8f,0xd9,0xff}};

static void badge_draw(const char *const *art, int rows, const BadgeColor *pal, int pal_n, float x, float y, float k) {
  // One rectangle per run of equal pixels in a row (not one per pixel): far fewer quads.
  for (int r = 0; r < rows; r++)
    for (int c = 0; art[r][c];) {
      char ch = art[r][c];
      int run = 1;
      while (art[r][c + run] == ch) run++;
      if (ch != '.')
        for (int p = 0; p < pal_n; p++)
          if (pal[p].ch == ch) {
            C2D_DrawRectSolid(x + c * k, y + r * k, 0.5f, k * run, k, C2D_Color32(pal[p].r, pal[p].g, pal[p].b, 255));
            break;
          }
      c += run;
    }
}

/** Width and height in native pixels of the badge of a device. */
void device_badge_size(int device, int *w, int *h) {
  if (device == DEVICE_SWITCH) { if (w) *w = 14; if (h) *h = 9; }
  else if (device == DEVICE_VITA) { if (w) *w = 12; if (h) *h = 9; }
  else { if (w) *w = 12; if (h) *h = 13; }
}

/** Badge of a native device (D.._3DS/_SWITCH/_VITA) with the top-left corner at (x, y). */
void device_badge(int device, float x, float y, float k) {
  if (device == DEVICE_SWITCH) badge_draw(BADGE_SWITCH, 9, PAL_SWITCH, COUNT(PAL_SWITCH), x, y, k);
  else if (device == DEVICE_VITA) badge_draw(BADGE_VITA, 9, PAL_VITA, COUNT(PAL_VITA), x, y, k);
  else badge_draw(BADGE_3DS, 13, PAL_3DS, COUNT(PAL_3DS), x, y, k);
}

/** 12×13 px icon drawn pixel by pixel (same as client/src/console-badge.ts). */
void console_badge(float x, float y, float k) { device_badge(DEVICE_3DS, x, y, k); }

static void fallback_floor(void) {
  u32 a = C2D_Color32(0xc5, 0xae, 0x88, 255), b = C2D_Color32(0xc8, 0xb2, 0x8d, 255);
  for (int y = 0; y < W.h; y++)
    for (int x = 0; x < W.w; x++) {
      if (!world_tile(x, y)) continue;
      float sx, sy, z = V.zoom;
      iso(x, y, &sx, &sy);
      float cx = scr_x(sx), top = scr_y(sy);
      u32 c = (x + y) % 2 ? a : b;
      C2D_DrawTriangle(cx, top, c, cx + HALF_W * z, top + HALF_H * z, c, cx - HALF_W * z, top + HALF_H * z, c, 0.5f);
      C2D_DrawTriangle(cx - HALF_W * z, top + HALF_H * z, c, cx + HALF_W * z, top + HALF_H * z, c, cx, top + TH * z, c, 0.5f);
    }
}

static void draw_bubbles(u64 now) {
  float dt = V.last_frame ? fminf(100, (float)(now - V.last_frame)) : 0;
  bool any = false;
  for (int i = 0; i < BUBBLE_MAX; i++) any |= V.chat[i].used;
  if (any && now - V.last_shift > BUBBLE_SCROLL_MS) {
    for (int i = 0; i < BUBBLE_MAX; i++) V.chat[i].row++;
    V.last_shift = now;
  }
  for (int i = 0; i < BUBBLE_MAX; i++) {
    Bubble *b = &V.chat[i];
    if (!b->used) continue;
    float target = BUBBLE_BASE - b->row * BUBBLE_ROW;
    b->y = isnan(b->y) ? target : fmaxf(target, b->y - dt * BUBBLE_SPEED);
    if (b->y < -BUBBLE_H || b->row >= BUBBLE_MAX) { b->used = false; continue; }
    float cx = scr_x(b->wx);
    // Dot rising from the head to the new bubble.
    float t = (now - b->t0) / 220.0f;
    if (t < 1) {
      float hy = scr_y(b->head_wy);
      C2D_DrawCircleSolid(cx, hy + (b->y - hy) * t, 0.5f, 3.5f, rgba(b->color, 255));
    }
    char name[32], text[112];
    snprintf(name, sizeof name, "%s: ", b->name);
    snprintf(text, sizeof text, "%s", b->text);
    float nw = text_width(name, BUBBLE_TEXT) + 1;
    text_fit(text, BUBBLE_TEXT, TOP_W - 30 - nw);
    float w = nw + text_width(text, BUBBLE_TEXT) + 18;
    float left = roundf(fmaxf(2, fminf(TOP_W - w - 2, cx - w / 2)));
    float top = roundf(b->y - BUBBLE_H);
    fill_round(left, top, w, BUBBLE_H, BUBBLE_H / 2.0f, rgba(b->color, 255));
    fill_round(left + 2, top + 2, w - 4, BUBBLE_H - 4, (BUBBLE_H - 4) / 2.0f, C2D_Color32(255, 255, 255, 255));
    // The system font has no bold: the name is drawn twice.
    float ty = top + 3;
    text_draw(name, left + 9, ty, BUBBLE_TEXT, C2D_Color32(0, 0, 0, 255), 0);
    text_draw(name, left + 9.6f, ty, BUBBLE_TEXT, C2D_Color32(0, 0, 0, 255), 0);
    text_draw(text, left + 9 + nw, ty, BUBBLE_TEXT, C2D_Color32(0, 0, 0, 255), 0);
  }
}

void scene_chat(u32 eid, const char *text, u64 now) {
  Ent *e = world_ent(eid);
  if (!e) return;
  float px, py, wx, wy;
  ent_pos(e, now, &px, &py);
  iso(px, py, &wx, &wy);
  u64 talk = 500 + strlen(text) * 70;
  e->talk_until = now + (talk > 3500 ? 3500 : talk);
  u32 c = look_shirt_rgb(e->look);
  // Nearly white shirts darken the border, like the web client.
  if ((c >> 16 & 255) + (c >> 8 & 255) + (c & 255) >= 600)
    c = (u32)((c >> 16 & 255) * .9f) << 16 | (u32)((c >> 8 & 255) * .9f) << 8 | (u32)((c & 255) * .9f);
  int slot = -1;
  for (int i = 0; i < BUBBLE_MAX; i++) {
    if (!V.chat[i].used) { if (slot < 0) slot = i; continue; }
    V.chat[i].row++;
  }
  if (slot < 0) {
    for (int i = 0; i < BUBBLE_MAX; i++) if (slot < 0 || V.chat[i].row > V.chat[slot].row) slot = i;
  }
  Bubble *b = &V.chat[slot];
  memset(b, 0, sizeof *b);
  b->used = true;
  snprintf(b->name, sizeof b->name, "%s", e->name);
  snprintf(b->text, sizeof b->text, "%s", text);
  b->color = c;
  b->wx = wx;
  b->head_wy = wy + HALF_H - 70 - ent_height(e, now);
  b->y = NAN;
  b->t0 = now;
  V.last_shift = now;
}

void scene_draw(u64 now) {
  gfx_filter(V.overview);
  if (!W.in_room) {
    NetState state = net_state();
    const char *status = state == NET_FULL ? "Tweebbo is at capacity" : state == NET_LOCKED ? "This room is locked" : state == NET_DENIED ? "Connection paused" : "Loading…";
    text_draw(status, TOP_W / 2, TOP_H / 2 - 8, 16, C2D_Color32(255, 255, 255, 255), 1);
    return;
  }
  if (pool_tower_view()) {
    // The jumper's own view: pool_tower.room over the whole screen.
    pool_draw_tower(now);
    draw_bubbles(now);
    draw_prompt();
    V.last_frame = now;
    return;
  }
  camera(now);
  if (V.scene) {
    // Sky and clouds under a rooftop first, then the floor.
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i < V.scene->count; i++)
        if (V.scene->sprites[i].background && (V.scene->sprites[i].role == 1) == (pass == 0)) {
          const SceneSprite *sp = &V.scene->sprites[i];
          gfx_scene_sprite(sp, scr_x(sp->x), scr_y(sp->y), V.zoom, sp->alpha);
        }
  } else if (V.bg) gfx_room_draw(V.bg, scr_x(V.bg->x), scr_y(V.bg->y), V.zoom);
  else fallback_floor();

  Ent *me = world_ent(W.me);
  if (now < V.dest_until) {
    if (me && me->x == V.dest_x && me->y == V.dest_y && !ent_moving(me, now) && V.dest_until > now + 500) V.dest_until = now + 500;
    tile_cursor(V.dest_x, V.dest_y, C2D_Color32(255, 255, 255, 240));
  }

  static Drawable list[4096];
  int n = 0;
  for (int i = 0; i < MAX_ITEMS && n < COUNT(list); i++) {
    const Item *it = &W.items[i];
    if (!it->used || (G.on && it->id == G.ignore)) continue;
    const FurniVariant *v = variant_frame(it->def, it->rot, it->state, furni_frame(it->def, it->state, now));
    if (!v) continue;
    for (int k = 0; k < v->count && n < COUNT(list); k++) {
      const FurniPart *p = &v->parts[k];
      float sx, sy;
      iso(it->x, it->y, &sx, &sy);
      float left = scr_x(sx + p->ox), top = scr_y(sy + p->oy - it->z);
      if (left >= TOP_W || top >= TOP_H || left + p->w * V.zoom <= 0 || top + p->h * V.zoom <= 0) continue;
      bool shadow = p->flags & PART_SHADOW;
      // Shadows are only cast from the floor or whole stacks.
      if (shadow && it->z % 32) continue;
      list[n] = (Drawable){shadow ? -10000 : it->x + it->y + p->depth + it->z / 1024.0f, n, it, p, NULL, NULL, NULL};
      n++;
    }
  }
  if (V.scene) {
    // Layers with their own depth (trees, cabins, water…); animated ones change every 120 ms.
    for (int i = 0; i < V.scene->count && n < COUNT(list); i++) {
      const SceneSprite *sp = &V.scene->sprites[i];
      if (sp->background) continue;
      if (sp->frames > 1 && sp->frame != (now / 120) % sp->frames) continue;
      // Booth curtains and the lift door: their open or closed picture (source/pool.c).
      if (sp->role >= 10 && (sp->role & 1) != pool_switch_closed((sp->role - 10) / 2)) continue;
      list[n] = (Drawable){sp->depth, n, NULL, NULL, NULL, sp, NULL};
      n++;
    }
  }
  pets_sync(now);
  for (int i = 0; i < pets_count() && n < COUNT(list); i++) {
    const Pet *pt = pet_at(i);
    float px, py;
    pet_pos(pt, now, &px, &py);
    list[n] = (Drawable){px + py + .002f, n, NULL, NULL, NULL, NULL, pt};
    n++;
  }
  static Item ghost_item;
  if (G.on) {
    // Footprint in green or red, and the translucent item above everything on its tile.
    int gw, gh;
    def_size(G.def, (u8)G.rot, &gw, &gh);
    if (!G.wall)
      for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) tile_cursor(G.x + x, G.y + y, G.ok ? C2D_Color32(0x7b, 0xd3, 0x89, 255) : C2D_Color32(0xff, 0x6b, 0x6b, 255));
    ghost_item = (Item){.used = true, .id = 0, .def = G.def, .x = (u8)G.x, .y = (u8)G.y, .rot = (u8)G.rot, .z = (u16)G.z};
    const FurniVariant *v = variant_get(G.def, def_rotation(G.def, (u8)G.rot), 0);
    for (int k = 0; v && k < v->count && n < COUNT(list); k++) {
      const FurniPart *p = &v->parts[k];
      if (p->flags & PART_SHADOW) continue;
      list[n] = (Drawable){G.x + G.y + .9f + p->depth * .001f, n, &ghost_item, p, NULL, NULL, NULL};
      n++;
    }
  }
  for (int i = 0; i < MAX_ENTS && n < COUNT(list); i++) {
    const Ent *e = &W.ents[i];
    if (!e->used || pool_hides(e)) continue;
    float px, py, sx, sy;
    ent_pos(e, now, &px, &py);
    // Off-screen avatars cost a dozen sprites each and draw nothing: skip them.
    iso(px, py, &sx, &sy);
    float ax = scr_x(sx), ay = scr_y(sy);
    if (ax < -80 * V.zoom || ax > TOP_W + 80 * V.zoom || ay < -40 * V.zoom || ay > TOP_H + 140 * V.zoom) continue;
    PoseInfo p = pose_of(e, now, ent_moving(e, now));
    list[n] = (Drawable){px + py + .002f + p.depth + ent_height(e, now) / 1024, n, NULL, NULL, e, NULL, NULL};
    n++;
  }
  qsort(list, (size_t)n, sizeof *list, by_depth);
  for (int i = 0; i < n; i++) {
    if (list[i].ent) draw_ent(list[i].ent, now);
    else if (list[i].pet) {
      float px, py, sx, sy;
      pet_pos(list[i].pet, now, &px, &py);
      iso(px, py, &sx, &sy);
      pet_draw(list[i].pet, now, scr_x(roundf(sx)), scr_y(roundf(sy)), V.zoom);
    } else if (list[i].layer) gfx_scene_sprite(list[i].layer, scr_x(list[i].layer->x), scr_y(list[i].layer->y), V.zoom, list[i].layer->alpha);
    else draw_item_part(list[i].item, list[i].part);
  }
  // Wobble Squabble on the pole, the countdown, the diver and the splashes.
  pool_draw_room(now);

  // Name of the selected avatar and an arrow over your own on arrival.
  for (int i = 0; i < MAX_ENTS; i++) {
    const Ent *e = &W.ents[i];
    if (!e->used) continue;
    float px, py, sx, sy;
    ent_pos(e, now, &px, &py);
    iso(px, py, &sx, &sy);
    if (e->id == V.selected) {
      float w = text_width(e->name, 11) + 10, x = scr_x(sx), y = scr_y(sy + HALF_H) + 6;
      fill_round(x - w / 2, y, w, 15, 4, C2D_Color32(0, 0, 0, 190));
      text_draw(e->name, x, y + 1, 11, C2D_Color32(255, 255, 255, 255), 1);
    }
    float raise = 0;
    if (appearance(e) & AV_FOUNDER) {
      PoseInfo p = pose_of(e, now, ent_moving(e, now));
      float bx = scr_x(sx + p.dx), by = scr_y(sy + HALF_H + p.dy - p.anchor_y + 11 - p.lift);
      float width = text_width("OWNER", 10) + 10;
      fill_rect(bx - width / 2, by - 15, width, 15, rgba(0xdfb54d, 255));
      fill_rect(bx - width / 2 + 1, by - 14, width - 2, 13, rgba(0x26373a, 255));
      text_draw("OWNER", bx, by - 15, 10, rgba(0xffe3a0, 255), 1);
      raise = 16;
    }
    int device = e->device ? e->device : (e->flags & FLAG_3DS ? DEVICE_3DS : 0);
    if (device) {
      // Pixel-art handheld of each native console (3DS/Switch/Vita), like the web.
      PoseInfo p = pose_of(e, now, ent_moving(e, now));
      float k = V.zoom < 1 ? 1 : V.zoom;
      float bx = scr_x(sx + p.dx), by = scr_y(sy + HALF_H + p.dy - p.anchor_y + 16 - p.lift);
      int bw = 12, bh = 13;
      device_badge_size(device, &bw, &bh);
      device_badge(device, bx - bw * 0.5f * k, by - bh * k - raise, k);
      raise += bh * k + 2;
    }
    if (e->id == W.me && now - V.joined_at < 7000) {
      float ax = scr_x(sx), ay = scr_y(sy + HALF_H - ANCHOR_Y + 22 - ent_height(e, now)) + sinf(now / 150.0f) * 3 - raise;
      u32 fill = C2D_Color32(0xff, 0xd1, 0x66, 255), ink = C2D_Color32(0, 0, 0, 255);
      C2D_DrawTriangle(ax - 8, ay - 13, ink, ax + 8, ay - 13, ink, ax, ay - 1, ink, 0.5f);
      C2D_DrawTriangle(ax - 6, ay - 12, fill, ax + 6, ay - 12, fill, ax, ay - 3, fill, 0.5f);
    }
  }
  draw_bubbles(now);
  draw_prompt();
  V.last_frame = now;
}

int scene_avatar_count(void) {
  int n = 0;
  for (int i = 0; i < MAX_ENTS; i++) n += W.ents[i].used;
  return n;
}

static char prompt[96];
void scene_prompt(const char *text) { snprintf(prompt, sizeof prompt, "%s", text ? text : ""); }

/** Classic capsule with the console's A button. */
static void draw_prompt(void) {
  if (!prompt[0]) return;
  char t[96];
  snprintf(t, sizeof t, "%s", prompt);
  text_fit(t, 11, TOP_W - 70);
  float w = text_width(t, 11) + 40, x = roundf((TOP_W - w) / 2), y = TOP_H - 26;
  fill_round(x, y, w, 21, 10, C2D_Color32(0, 0, 0, 255));
  fill_round(x + 1, y + 1, w - 2, 19, 9, C2D_Color32(255, 255, 255, 245));
  C2D_DrawCircleSolid(x + 13, y + 10.5f, 0.5f, 8, C2D_Color32(0x2b, 0x31, 0x38, 255));
  text_draw("A", x + 13, y + 4, 10, C2D_Color32(255, 255, 255, 255), 1);
  text_draw(t, x + 26, y + 4, 11, C2D_Color32(0x22, 0x22, 0x22, 255), 0);
}

void scene_to_screen(float wx, float wy, float *x, float *y) { *x = scr_x(wx); *y = scr_y(wy); }
float scene_zoom(void) { return V.zoom; }
