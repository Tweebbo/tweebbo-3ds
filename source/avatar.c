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
  // Bits 32-41 (web client/src/avatar.ts): face + 1, then the high bits of the later hair (1),
  // bottoms (2) and shoes (2). Items this console does not have fall back to the first one.
  u32 lo = (u32)n, hi = (u32)(n >> 32);
  Look l = {
    .skin = lo & 3,
    .hair_style = (int)(((lo >> 2) & 7) | (((lo >> 19) & 7) << 3) | (((hi >> 5) & 1) << 6)),
    .hair_color = (lo >> 5) & 7,
    .shirt_color = (lo >> 8) & 15,
    .shirt_style = (int)(((lo >> 12) & 3) | (((lo >> 22) & 31) << 2)),
    .pants = (lo >> 14) & 7,
    .shoes = (lo >> 17) & 3,
    .pants_style = (int)(((lo >> 27) & 7) | (((hi >> 6) & 3) << 3)),
    .shoe_style = (int)(((lo >> 30) & 3) | (((hi >> 8) & 3) << 2)),
    .face_style = (hi & 31) ? (int)(hi & 31) - 1 : 0,
  };
  return l;
}

u64 look_encode(const Look *l) {
  u32 base = (u32)l->skin | (u32)(l->hair_style & 7) << 2 | (u32)l->hair_color << 5 | (u32)l->shirt_color << 8 |
             (u32)(l->shirt_style & 3) << 12 | (u32)l->pants << 14 | (u32)l->shoes << 17 |
             (u32)((l->hair_style >> 3) & 7) << 19 | (u32)(l->shirt_style >> 2) << 22 | (u32)(l->pants_style & 7) << 27 |
             (u32)(l->shoe_style & 3) << 30;
  u64 hi = (u64)(l->face_style + 1) | (u64)((l->hair_style >> 6) & 1) << 5 | (u64)((l->pants_style >> 3) & 3) << 6 |
           (u64)((l->shoe_style >> 2) & 3) << 8;
  return base + (hi << 32);
}

u32 look_shirt_rgb(u64 look) { return SHIRT[look_decode(look).shirt_color]; }

// Parts in the order of the internal indices.
enum { LH, LS, BD, SH, LG, CH, HD, FC, EY, HR, RH, RS, RI, CP, CC, WA, CA, LC, RC, FA, EA, HRB, HA, HE, PARTS };
static const char *PART[PARTS] = {"lh", "ls", "bd", "sh", "lg", "ch", "hd", "fc", "ey", "hr", "rh", "rs", "ri", "cp", "cc", "wa", "ca", "lc", "rc", "fa", "ea", "hrb", "ha", "he"};
static const s8 FLIP[8] = {0, 1, 2, 3, 2, 1, 0, 7};

static int part_index(const char *code) {
  for (int i = 0; i < PARTS; i++) if (!strncmp(code, PART[i], strlen(PART[i])) && (code[strlen(PART[i])] == ':' || !code[strlen(PART[i])])) return i;
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

void avatar_draw_held(int drink, float cx, float cy, float scale) {
  char name[32];
  snprintf(name, sizeof name, "h_crr_ri_%s_2_0", drink_model(drink));
  const PeopleSprite *s = find_sprite(name);
  if (s) gfx_sprite(s->page, s->x, s->y, s->w, s->h, cx - s->w * scale / 2, cy - s->h * scale / 2, scale, false, 0xffffffff);
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
typedef struct { u64 look; s16 dir, pose, frame, expr, head, drink, sip, head_only; u32 used; u8 n; AvatarOp ops[64]; } AvatarEntry;
#define AV_CACHE 256
static AvatarEntry av_cache[AV_CACHE];
static u32 av_clock;
static AvatarOp *rec_ops;
static int rec_n;

static void avatar_compose(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, int drink, bool sip);

static u64 resolved_look(u64 look) {
  u64 bathing = look & AV_BATHING;
  look &= ~AV_BATHING;
  if (look & AV_FOUNDER) {
    Look owner = {.skin=0,.face_style=0,.hair_style=12,.hair_color=5,.shirt_style=1,.shirt_color=0,.pants=1};
    look = look_encode(&owner) | AV_FOUNDER;
  } else if (look & AV_FRANK) {
    Look frank = {.skin=0,.face_style=0,.hair_style=0,.hair_color=6,.shirt_style=4,.shirt_color=2,.pants=5};
    look = look_encode(&frank) | AV_FRANK;
  }
  return look | bathing;
}

char look_gender(u64 look) {
  look &= ~(AV_FRANK | AV_FOUNDER | AV_BATHING);
  Look L = look_decode(look);
  return look >= (1ull << 32)
           ? wardrobe_faces[L.face_style < wardrobe_faces_count ? L.face_style : 0].sex
           : wardrobe_tops[L.shirt_style < wardrobe_tops_count ? L.shirt_style : 0].sex;
}

#include "gen/character_skins.h"
void avatar_draw(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, int drink, bool sip,
                 float x, float y, float scale, u8 alpha) {
  int skin = (int)((look >> 48) & 255);
  if (skin >= 1 && skin <= CHARACTER_SKIN_COUNT) {
    int p = pose <= POSE_DANCE ? pose : POSE_STD, d = dir & 7;
    // osGetTime is calendar time: the accumulated frame number exceeds 32 bits.
    u64 tick = (u64)(now_ms() * (double)character_skin_fps[skin-1][p][d] / 1000);
    int f = tick % character_skin_frames[skin-1][p][d];
    char name[32];snprintf(name, sizeof name, "k_%d_%d_%d_%d", skin, p, d, f);
    const PeopleSprite *s = find_sprite(name);
    if (s) gfx_sprite(s->page,s->x,s->y,s->w,s->h,x+s->rx*scale,y+s->ry*scale,scale,false,abgr(0xffffff,alpha));
    return;
  }
  look = resolved_look(look);
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

static const PeopleSprite *pose_sprite(char prefix, const char *action, int part, const char *model, int d, int f, bool lay) {
  char name[64];
  snprintf(name, sizeof name, "%c_%s_%s_%s_%d_%d", prefix, action, PART[part], model, d, f);
  const PeopleSprite *s = find_sprite(name);
  if (!s) { snprintf(name, sizeof name, "%c_%s_%s_%s_%d_0", prefix, action, PART[part], model, d); s = find_sprite(name); }
  if (!s) { snprintf(name, sizeof name, "%c_%s_%s_%s_%d_0", prefix, lay ? "lay" : "std", PART[part], model, d); s = find_sprite(name); }
  return s;
}

static void record_part(const PeopleSprite *s, int ox, int oy, Pose pose, bool mirrored, u32 color) {
  if (!s || rec_n >= 64) return;
  bool lay = pose == POSE_LAY;
  int px = -s->rx + ox + (lay ? 30 : 0);
  int py = (lay ? 50 : ANCHOR_Y - 2) - s->ry + oy + (pose == POSE_FLOOR ? 18 : 0);
  if (mirrored) px = (lay ? 89 : SPRITE_W) - px - s->w;
  rec_ops[rec_n++] = (AvatarOp){s, (s16)px, (s16)py, color};
}

static void avatar_compose(u64 look, int dir, Pose pose, int frame, int expr, int head_dir, int drink, bool sip) {
  // On the deck a swimsuit is worn on the full-size body (avatar.ts): the head, every direction
  // and every animation match the usual figure; only the clothes change.
  bool bathing = (look & AV_BATHING) != 0;
  char sex = bathing ? look_gender(look) : 'M';
  look &= ~AV_BATHING;
  if (bathing) drink = 0;
  Look L = look_decode(look);
  bool founder = (look & AV_FOUNDER) != 0, frank = !founder && (look & AV_FRANK) != 0;
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
  struct { int part, tint; char model[8]; } layers[48];
  int layer_n = 0;
  const WardrobeEntry *entries[] = {
    &wardrobe_faces[face], &wardrobe_hair[L.hair_style < wardrobe_hair_count ? L.hair_style : 0],
    &wardrobe_tops[L.shirt_style < wardrobe_tops_count ? L.shirt_style : 0],
    &wardrobe_bottoms[L.pants_style < wardrobe_bottoms_count ? L.pants_style : 0],
    &wardrobe_shoes[L.shoe_style < wardrobe_shoes_count ? L.shoe_style : 0]
  };
  for (int i = 0; i < 5; i++) {
    const char *p = entries[i]->layers;
    while (p && *p && layer_n < 48) {
      char part[4], model[8]; int tint, used = 0;
      if (sscanf(p, "%3[^:]:%7[^:]:%d%n", part, model, &tint, &used) != 3 || used <= 0) break;
      layers[layer_n].part = part_index(part); layers[layer_n].tint = tint;
      snprintf(layers[layer_n++].model, 8, "%s", model);
      p += used; while (*p == ' ') p++;
    }
  }
  if (founder || frank) snprintf(models[CC], 8, "2016");
  if (frank) { models[HR][0] = 0; snprintf(models[FA], 8, "4"); snprintf(models[HA], 8, "2207"); }
  if (drink && !lay) snprintf(models[RI], 8, "%s", drink_model(drink));

  u32 colors[PARTS];
  for (int i = 0; i < PARTS; i++) colors[i] = 0xffffff;
  colors[BD] = colors[HD] = colors[FC] = colors[LH] = colors[RH] = SKIN[L.skin];
  colors[HR] = HAIR[L.hair_color];
  colors[CH] = colors[LS] = colors[RS] = SHIRT[L.shirt_color];
  colors[LG] = PANTS[L.pants];
  colors[SH] = SHOES[L.shoes];

  colors[HRB] = colors[FA] = HAIR[L.hair_color];
  colors[CP] = colors[CC] = colors[WA] = colors[CA] = colors[LC] = colors[RC] = colors[EA] = colors[HA] = colors[HE] = SHIRT[L.shirt_color];
  if (founder) {
    colors[BD] = colors[HD] = colors[FC] = colors[LH] = colors[RH] = 0xffe1cc;
    colors[HR] = 0x22283e; colors[CH] = 0xf4ecda; colors[LS] = colors[RS] = 0x14434c;
    colors[LG] = 0x303036; colors[SH] = 0x1b2026;
  }
  int order[PARTS + 1], n = 0;
  static const int base[] = {LH, LS, BD, SH, LG, CH, HD, FC, EY, HR, RH, RS};
  static const int back[] = {LH, LS, RH, RS, BD, SH, LG, CH, HD, FC, EY, HR};
  static const int side[] = {BD, SH, LG, CH, HD, FC, EY, LH, LS, HR, RH, RS};
  const int *src = direction == 7 ? back : direction == 3 ? side : base;
  for (int i = 0; i < 12; i++) {
    if (models[RI][0] && src[i] == RH) order[n++] = RI;
    if (src[i] == HR) order[n++] = HRB;
    order[n++] = src[i];
    if (src[i] == CH) { order[n++] = CP; order[n++] = CC; order[n++] = WA; order[n++] = CA; }
    if (src[i] == LS) order[n++] = LC;
    if (src[i] == RS) order[n++] = RC;
    if (src[i] == FC) order[n++] = FA;
    if (src[i] == EY) order[n++] = EA;
    if (src[i] == HR) { order[n++] = HA; order[n++] = HE; }
  }
  bool seated = pose == POSE_SIT || pose == POSE_FLOOR;
  if (seated && FLIP[direction] != 0) {
    int a = -1, b = -1;
    for (int i = 0; i < n; i++) { if (order[i] == SH) a = i; if (order[i] == LG) b = i; }
    if (a >= 0 && b >= 0) { int t = order[a]; order[a] = order[b]; order[b] = t; }
  }

  for (int k = 0; k < n; k++) {
    int part = order[k];
    bool head = part == HD || part == FC || part == EY || part == HR || part == HRB || part == HA || part == HE || part == EA || part == FA;
    if (head_only && !head) continue;
    int d = FLIP[head && !lay ? head_dir & 7 : direction];
    const char *action = lay ? "lay" : seated ? "sit" : pose == POSE_WALK ? "wlk" : "std";
    int f = (!strcmp(action, "wlk") && !head && part != CH) ? frame & 3 : 0;
    int off_x = 0, off_y = 0;
    if (pose == POSE_WAVE && (part == LH || part == LS)) { action = "wav"; f = frame & 1; }
    if (pose == POSE_DANCE) {
      const DanceTrack *t = dance_track(founder && part == CC ? CH : part);
      if (t && t->n) {
        int i = frame % t->n;
        action = t->act[i]; f = t->frm[i]; off_x = t->offx[i]; off_y = t->offy[i];
        d = FLIP[(direction + t->offd[i] + 8) & 7];
      }
    }
    if ((expr & EXPR_TALK) && (part == HD || part == FC || part == HR)) { action = lay ? "lsp" : "spk"; f = frame & 1; }
    if (!lay && (expr & EXPR_BLINK) && part == EY) { action = "eyb"; f = 0; }
    if (drink && !lay && (part == RI || part == RH || part == RS)) { action = sip ? "drk" : "crr"; f = 0; }
    if (bathing && !head && part != LH && part != BD && part != RH) {
      // Swimwear: the original "Mini shorts" (lg 2027) as trunks, plus the plain torso (ch 001)
      // in the same colour for a women's one-piece.
      if (part == LG) record_part(pose_sprite('x', action, LG, "2027", d, f, lay), off_x, off_y, pose, mirrored, SHIRT[L.shirt_color]);
      if (part == CH && sex == 'F') record_part(pose_sprite('h', action, CH, "001", d, f, lay), off_x, off_y, pose, mirrored, SHIRT[L.shirt_color]);
      continue;
    }

    for (int j = 0; j < layer_n; j++) if (layers[j].part == part) {
      const PeopleSprite *tint = pose_sprite('x', action, part, layers[j].model, d, f, lay);
      const PeopleSprite *fixed = pose_sprite('z', action, part, layers[j].model, d, f, lay);
      record_part(tint, off_x, off_y, pose, mirrored, layers[j].tint == 1 ? colors[part] : 0xffffff);
      record_part(fixed, off_x, off_y, pose, mirrored, 0xffffff);
    }
    if (!models[part][0]) continue;
    char prefix = founder && part == CC ? 'o' : frank && (part == CC || part == FA || part == HA) ? 'f' : 'h';
    const PeopleSprite *s = pose_sprite(prefix, action, part, models[part], d, f, lay);
    record_part(s, off_x, off_y, pose, mirrored, prefix == 'h' ? colors[part] : 0xffffff);
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
  look = resolved_look(look);
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
  bool drew_head = false;
  for (int k = 0; k < n; k++) {
    int part = order[k];
    bool head = part == HD || part == FC || part == EY || part == HR;
    if (head) {
      if (!drew_head) {
        float bob = pose == POSE_FLOAT && frame > 1 ? 2 : 0;
        head_only = true;
        avatar_draw(look, dir, POSE_STD, frame, expr, head_dir, 0, false, x + 48 * scale, y + bob * scale, scale, 255);
        head_only = false;
        drew_head = true;
      }
      continue;
    }
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
    if ((look & AV_FOUNDER) && part != CH) color = 0xffe1cc;
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
