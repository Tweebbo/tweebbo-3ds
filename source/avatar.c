#include "avatar.h"
#include "assets.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

// Web client palettes (avatar.ts), as 0xRRGGBB.
const u32 SKIN[4] = {0xf6d3b3, 0xe9b88f, 0xc98d60, 0x8a5838};
const u32 HAIR[8] = {0x2a1f17, 0x5b3a22, 0x9a6430, 0xe2c06e, 0xc4452c, 0x22223a, 0xdcdcdc, 0x7c4fa3};
const u32 SHIRT[16] = {0xececec, 0x2b2b2f, 0xc83a2e, 0xe8822a, 0xf0c330, 0x2fa45a, 0x17a08c, 0x2d7fc4,
                       0x33475f, 0x8b47b0, 0xe2508f, 0x9c5a32, 0x7f8a8c, 0xff8a80, 0x7fbcf5, 0x5ee6c0};
const u32 PANTS[8] = {0x2e3b5e, 0x1f1f22, 0x6d4f39, 0x80898c, 0x3e6b48, 0x9a3030, 0xc9b28a, 0x4c4c8c};
const u32 SHOES[4] = {0x1e1e1e, 0x6b3e26, 0xededed, 0xb5372b};

Look look_decode(u64 n) {
  u32 lo = (u32)n;
  Look l = {
    .skin = lo & 3,
    .hair_style = (int)(((lo >> 2) & 7) | (((lo >> 19) & 7) << 3)),
    .hair_color = (lo >> 5) & 7,
    .shirt_color = (lo >> 8) & 15,
    .shirt_style = (int)(((lo >> 12) & 3) | (((lo >> 22) & 31) << 2)),
    .pants = (lo >> 14) & 7,
    .shoes = (lo >> 17) & 3,
    .pants_style = (lo >> 27) & 7,
    .shoe_style = (lo >> 30) & 3,
    .face_style = (n >> 32) ? (int)(n >> 32) - 1 : 0,
  };
  return l;
}

u64 look_encode(const Look *l) {
  u32 base = (u32)l->skin | (u32)(l->hair_style & 7) << 2 | (u32)l->hair_color << 5 | (u32)l->shirt_color << 8 |
             (u32)(l->shirt_style & 3) << 12 | (u32)l->pants << 14 | (u32)l->shoes << 17 |
             (u32)(l->hair_style >> 3) << 19 | (u32)(l->shirt_style >> 2) << 22 | (u32)l->pants_style << 27 |
             (u32)l->shoe_style << 30;
  return base + ((u64)l->face_style + 1) * ((u64)1 << 32);
}

u32 look_shirt_rgb(u64 look) { return SHIRT[look_decode(look).shirt_color]; }

// Parts in the order of the internal indices.
enum { LH, LS, BD, SH, LG, CH, HD, FC, EY, HR, RH, RS, RI, PARTS };
static const char *PART[PARTS] = {"lh", "ls", "bd", "sh", "lg", "ch", "hd", "fc", "ey", "hr", "rh", "rs", "ri"};
static const s8 FLIP[8] = {0, 1, 2, 3, 2, 1, 0, 7};

static int part_index(const char *code) {
  for (int i = 0; i < PARTS; i++) if (code[0] == PART[i][0] && code[1] == PART[i][1]) return i;
  return -1;
}

/** Applies "hr:001 hd:002 …" from a wardrobe entry. */
static void apply(char models[PARTS][8], const WardrobeEntry *e) {
  const char *p = e->parts;
  while (*p) {
    while (*p == ' ') p++;
    if (!p[0] || !p[1] || p[2] != ':') break;
    int i = part_index(p);
    p += 3;
    int n = 0;
    char model[8];
    while (*p && *p != ' ' && n < 7) model[n++] = *p++;
    model[n] = 0;
    if (i >= 0) memcpy(models[i], model, 8);
  }
}

static const PeopleSprite *find_sprite(const char *name) {
  int lo = 0, hi = people_sprite_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(people_sprites[mid].name, name);
    if (!c) return &people_sprites[mid];
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

static const DanceTrack *dance_track(int part) {
  for (int i = 0; i < dance_track_count; i++)
    if (dance_tracks[i].index == 1 && !strcmp(dance_tracks[i].part, PART[part])) return &dance_tracks[i];
  return NULL;
}

static const char *drink_model(int drink) {
  for (int i = 0; i < drink_model_count; i++) if (drink_models[i].drink == drink) return drink_models[i].model;
  return "001";
}

static u32 abgr(u32 rgb, u8 alpha) {
  return (u32)alpha << 24 | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff);
}

static bool head_only;

void avatar_draw_head(u64 look, int dir, float x, float y, float scale) {
  head_only = true;
  avatar_draw(look, dir, POSE_STD, 0, 0, dir, 0, false, x, y, scale, 255);
  head_only = false;
}

// Composition cache: resolving part names (snprintf + search) for every avatar on every
// frame is costly on Old 3DS/2DS. The resolved part list is kept per state combination.
typedef struct { const PeopleSprite *s; s16 px, py; u32 color; } AvatarOp;
typedef struct { u64 look; s16 dir, pose, frame, expr, head, drink, sip, head_only; u32 used; u8 n; AvatarOp ops[16]; } AvatarEntry;
#define AV_CACHE 96
static AvatarEntry av_cache[AV_CACHE];
static u32 av_clock;
static AvatarOp *rec_ops;
static int rec_n;

static void avatar_compose(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, int drink, bool sip);

void avatar_draw(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, int drink, bool sip,
                 float x, float y, float scale, u8 alpha) {
  AvatarEntry *e = NULL, *victim = &av_cache[0];
  for (int i = 0; i < AV_CACHE; i++) {
    AvatarEntry *c = &av_cache[i];
    if (c->used && c->look == look && c->dir == dir && c->pose == (s16)pose && c->frame == frame && c->expr == expr &&
        c->head == head_dir && c->drink == drink && c->sip == sip && c->head_only == head_only) { e = c; break; }
    if (c->used < victim->used) victim = c;
  }
  if (!e) {
    e = victim;
    *e = (AvatarEntry){look, (s16)dir, (s16)pose, (s16)frame, (s16)expr, (s16)head_dir, (s16)drink, sip, head_only, 0, 0, {{0}}};
    rec_ops = e->ops;
    rec_n = 0;
    avatar_compose(look, dir, pose, frame, expr, head_dir, drink, sip);
    e->n = (u8)rec_n;
  }
  e->used = ++av_clock;
  bool lay = pose == POSE_LAY;
  int direction = lay ? (dir == 0 ? 4 : 2) : dir & 7;
  bool mirrored = direction >= 4 && direction <= 6;
  for (int i = 0; i < e->n; i++) {
    const AvatarOp *o = &e->ops[i];
    gfx_sprite(o->s->page, o->s->x, o->s->y, o->s->w, o->s->h, x + o->px * scale, y + o->py * scale, scale, mirrored,
               abgr(o->color, alpha));
  }
}

static void avatar_compose(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, int drink, bool sip) {
  Look L = look_decode(look);
  bool lay = pose == POSE_LAY;
  int direction = lay ? (dir == 0 ? 4 : 2) : dir & 7;
  bool mirrored = direction >= 4 && direction <= 6;
  char models[PARTS][8];
  memset(models, 0, sizeof models);
  int face = L.face_style < wardrobe_faces_count ? L.face_style : 0;
  apply(models, &wardrobe_faces[face]);
  apply(models, &wardrobe_hair[L.hair_style < wardrobe_hair_count ? L.hair_style : 0]);
  apply(models, &wardrobe_tops[L.shirt_style < wardrobe_tops_count ? L.shirt_style : 0]);
  apply(models, &wardrobe_bottoms[L.pants_style < wardrobe_bottoms_count ? L.pants_style : 0]);
  apply(models, &wardrobe_shoes[L.shoe_style < wardrobe_shoes_count ? L.shoe_style : 0]);
  if (drink && !lay) snprintf(models[RI], 8, "%s", drink_model(drink));

  u32 colors[PARTS];
  for (int i = 0; i < PARTS; i++) colors[i] = 0xffffff;
  colors[BD] = colors[HD] = colors[FC] = colors[LH] = colors[RH] = SKIN[L.skin];
  colors[HR] = HAIR[L.hair_color];
  colors[CH] = colors[LS] = colors[RS] = SHIRT[L.shirt_color];
  colors[LG] = PANTS[L.pants];
  colors[SH] = SHOES[L.shoes];

  int order[PARTS + 1], n = 0;
  static const int base[] = {LH, LS, BD, SH, LG, CH, HD, FC, EY, HR, RH, RS};
  static const int back[] = {LH, LS, RH, RS, BD, SH, LG, CH, HD, FC, EY, HR};
  static const int side[] = {BD, SH, LG, CH, HD, FC, EY, LH, LS, HR, RH, RS};
  const int *src = direction == 7 ? back : direction == 3 ? side : base;
  for (int i = 0; i < 12; i++) {
    if (models[RI][0] && src[i] == RH) order[n++] = RI;
    order[n++] = src[i];
  }
  bool seated = pose == POSE_SIT || pose == POSE_FLOOR;
  if (seated && FLIP[direction] != 0) {
    int a = -1, b = -1;
    for (int i = 0; i < n; i++) { if (order[i] == SH) a = i; if (order[i] == LG) b = i; }
    if (a >= 0 && b >= 0) { int t = order[a]; order[a] = order[b]; order[b] = t; }
  }

  int canvas_w = lay ? 89 : SPRITE_W;
  for (int k = 0; k < n; k++) {
    int part = order[k];
    if (!models[part][0]) continue;
    bool head = part == HD || part == FC || part == EY || part == HR;
    if (head_only && !head) continue;
    int d = FLIP[head && !lay ? head_dir & 7 : direction];
    const char *action = lay ? "lay" : seated ? "sit" : pose == POSE_WALK ? "wlk" : "std";
    int f = (!strcmp(action, "wlk") && !head && part != CH) ? frame & 3 : 0;
    int off_x = 0, off_y = 0;
    if (pose == POSE_WAVE && (part == LH || part == LS)) { action = "wav"; f = frame & 1; }
    if (pose == POSE_DANCE) {
      const DanceTrack *t = dance_track(part);
      if (t && t->n) {
        int i = frame % t->n;
        action = t->act[i]; f = t->frm[i]; off_x = t->offx[i]; off_y = t->offy[i];
        d = FLIP[(direction + t->offd[i] + 8) & 7];
      }
    }
    if ((expr & EXPR_TALK) && (part == HD || part == FC || part == HR)) { action = lay ? "lsp" : "spk"; f = frame & 1; }
    if (!lay && (expr & EXPR_BLINK) && part == EY) { action = "eyb"; f = 0; }
    if (drink && !lay && (part == RI || part == RH || part == RS)) { action = sip ? "drk" : "crr"; f = 0; }

    char name[48];
    const PeopleSprite *s;
    snprintf(name, sizeof name, "h_%s_%s_%s_%d_%d", action, PART[part], models[part], d, f);
    s = find_sprite(name);
    if (!s) { snprintf(name, sizeof name, "h_%s_%s_%s_%d_0", action, PART[part], models[part], d); s = find_sprite(name); }
    if (!s) { snprintf(name, sizeof name, "h_%s_%s_%s_%d_0", lay ? "lay" : "std", PART[part], models[part], d); s = find_sprite(name); }
    if (!s) continue;
    int px = -s->rx + off_x + (lay ? 30 : 0);
    int py = (lay ? 50 : ANCHOR_Y - 2) - s->ry + off_y + (pose == POSE_FLOOR ? 18 : 0);
    // On the mirrored canvas the part covers [W - px - w, W - px].
    if (mirrored) px = canvas_w - px - s->w;
    if (rec_n < 16) rec_ops[rec_n++] = (AvatarOp){s, (s16)px, (s16)py, colors[part]};
  }
}

static const PeopleSprite *find_swim(const char *name) {
  int lo = 0, hi = swim_sprite_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(swim_sprites[mid].name, name);
    if (!c) return &swim_sprites[mid];
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

void avatar_draw_swim(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, float x, float y, float scale) {
  Look L = look_decode(look);
  int direction = dir & 7;
  bool mirrored = direction >= 4 && direction <= 6;
  char models[PARTS][8];
  memset(models, 0, sizeof models);
  apply(models, &wardrobe_faces[L.face_style < wardrobe_faces_count ? L.face_style : 0]);
  apply(models, &wardrobe_hair[L.hair_style < wardrobe_hair_count ? L.hair_style : 0]);
  // lookGender(): with an extended face the face decides; otherwise the top.
  char sex = look >= (1ull << 32) ? wardrobe_faces[L.face_style < wardrobe_faces_count ? L.face_style : 0].sex
                                  : wardrobe_tops[L.shirt_style < wardrobe_tops_count ? L.shirt_style : 0].sex;
  static const int heads[] = {HD, FC, EY, HR};
  int order[8], n = 0;
  if (direction == 7) { order[n++] = LH; order[n++] = RH; order[n++] = BD; order[n++] = CH; for (int i = 0; i < 4; i++) order[n++] = heads[i]; }
  else if (direction == 3) { order[n++] = BD; order[n++] = CH; for (int i = 0; i < 4; i++) order[n++] = heads[i]; order[n++] = LH; order[n++] = RH; }
  else { order[n++] = LH; order[n++] = BD; order[n++] = CH; for (int i = 0; i < 4; i++) order[n++] = heads[i]; order[n++] = RH; }
  const float W_ = 160;
  for (int k = 0; k < n; k++) {
    int part = order[k];
    bool head = part == HD || part == FC || part == EY || part == HR;
    int d = FLIP[head ? head_dir & 7 : direction];
    const char *model = head ? models[part] : part == CH ? (sex == 'F' ? "s01" : "s02") : "s01";
    if (!model[0]) continue;
    const char *action = head ? "std" : pose == POSE_SWIM ? "swm" : "sws";
    int f = head ? 0 : frame;
    if (head && (expr & EXPR_TALK) && part != EY) { action = "spk"; f = frame & 1; }
    if (part == EY && (expr & EXPR_BLINK)) action = "eyb";
    char name[48];
    const PeopleSprite *s;
    snprintf(name, sizeof name, "%s_%s_%s_%s_%d_%d", head ? "h" : "sh", action, PART[part], model, d, f);
    s = head ? find_sprite(name) : find_swim(name);
    if (!s) { snprintf(name, sizeof name, "%s_%s_%s_%s_%d_0", head ? "h" : "sh", action, PART[part], model, d); s = head ? find_sprite(name) : find_swim(name); }
    if (!s && head) { snprintf(name, sizeof name, "h_std_%s_%s_%d_0", PART[part], model, d); s = find_sprite(name); }
    if (!s) continue;
    u32 color = part == HR ? HAIR[L.hair_color] : part == CH ? SHIRT[L.shirt_color] : part == EY ? 0xffffff : SKIN[L.skin];
    // Swimming bodies are reduced: the original doubles their registration.
    float factor = head ? 1 : 2, bob = pose == POSE_FLOAT && frame > 1 ? 2 : 0;
    float px = 48 - s->rx * factor, py = ANCHOR_Y - 2 - s->ry * factor + bob, w = s->w * factor;
    if (mirrored) px = W_ - px - w;
    float dx = x + px * scale, dy = y + py * scale;
    gfx_sprite_xy(s->page, s->x, s->y, s->w, s->h, dx, dy, factor * scale, factor * scale, mirrored, abgr(color, 255));
    // Teal water veil at 60 % over the body (source-atop on the web).
    if (!head) gfx_sprite_solid(s->page, s->x, s->y, s->w, s->h, dx, dy, factor * scale, factor * scale, mirrored, abgr(0x009c9c, 153));
  }
}
