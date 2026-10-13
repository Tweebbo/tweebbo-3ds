// Room pets (PETS): interpolation and composition like pets.ts
// (petSprite, mergePet, petVisual) with one texture per breed palette.
#include "pets.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "world.h"

static Pet pets[MAX_PETS];
static int pet_n;
static u32 seen_rev;
static const char *seen_json;

int pets_count(void) { return pet_n; }
const Pet *pet_at(int i) { return &pets[i]; }

static float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

void pet_pos(const Pet *p, u64 now, float *x, float *y) {
  float t = now > p->t0 ? clamp01((float)(now - p->t0) / 500) : 0;
  *x = p->fx + (p->x - p->fx) * t;
  *y = p->fy + (p->y - p->fy) * t;
}

static bool moving(const Pet *p, u64 now) { return now - p->t0 < 500 && (p->fx != p->x || p->fy != p->y); }

void pets_sync(u64 now) {
  if (seen_rev == W.pets_rev && seen_json == W.pets_json) return;
  seen_rev = W.pets_rev;
  seen_json = W.pets_json;
  json_error_t err;
  json_t *j = W.pets_json ? json_loads(W.pets_json, 0, &err) : NULL;
  if (!j) { pet_n = 0; return; }
  Pet next[MAX_PETS];
  int n = 0;
  size_t i;
  json_t *v;
  json_array_foreach(j, i, v) {
    if (n >= MAX_PETS) break;
    Pet p;
    memset(&p, 0, sizeof p);
    p.id = (int)json_integer_value(json_object_get(v, "id"));
    p.owner = (u32)json_integer_value(json_object_get(v, "owner"));
    p.kind = (int)json_integer_value(json_object_get(v, "kind"));
    p.breed = (int)json_integer_value(json_object_get(v, "breed"));
    p.color = (int)json_integer_value(json_object_get(v, "color"));
    p.x = (int)json_integer_value(json_object_get(v, "x"));
    p.y = (int)json_integer_value(json_object_get(v, "y"));
    p.dir = (int)json_integer_value(json_object_get(v, "dir")) & 7;
    p.z = (int)json_integer_value(json_object_get(v, "z"));
    snprintf(p.name, sizeof p.name, "%s", json_string_value(json_object_get(v, "name")) ?: "");
    snprintf(p.action, sizeof p.action, "%s", json_string_value(json_object_get(v, "action")) ?: "std");
    // mergePet: keeps the interpolation when the tile did not change.
    const Pet *old = NULL;
    for (int k = 0; k < pet_n; k++) if (pets[k].id == p.id) old = &pets[k];
    if (!old) { p.fx = (float)p.x; p.fy = (float)p.y; p.t0 = now; }
    else if (old->x == p.x && old->y == p.y) { p.fx = old->fx; p.fy = old->fy; p.t0 = old->t0; }
    else { pet_pos(old, now, &p.fx, &p.fy); p.t0 = now; }
    next[n++] = p;
  }
  json_decref(j);
  memcpy(pets, next, sizeof(Pet) * (size_t)n);
  pet_n = n;
}

static const PetPart *find_part(const char *name) {
  int lo = 0, hi = pet_part_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(pet_parts[mid].name, name);
    if (!c) return &pet_parts[mid];
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

static const PetAlias *find_alias(const char *name) {
  int lo = 0, hi = pet_alias_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(pet_aliases[mid].from, name);
    if (!c) return &pet_aliases[mid];
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

/** Follows aliases (some mirrored) to a real part. */
static const PetPart *resolve(const char *name, bool *flip) {
  char cur[48];
  snprintf(cur, sizeof cur, "%s", name);
  *flip = false;
  for (int guard = 0; guard < 8; guard++) {
    const PetAlias *a = find_alias(cur);
    if (!a) break;
    *flip = *flip != (a->flip != 0);
    snprintf(cur, sizeof cur, "%s", a->to);
  }
  return find_part(cur);
}

static const PetOffset *find_offset(const char *key) {
  int lo = 0, hi = pet_offset_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(pet_offsets[mid].key, key);
    if (!c) return &pet_offsets[mid];
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

static int palette_index(const Pet *p) {
  char name[32];
  snprintf(name, sizeof name, "Palette %s %03d", p->kind == 2 ? "croco" : "dogcat", p->breed);
  for (int i = 0; i < pet_palette_count; i++) if (!strcmp(pet_palettes[i], name)) return i;
  return 0;
}

static u32 abgr(u32 rgb, u8 a) { return (u32)a << 24 | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff); }

void pet_draw(const Pet *p, u64 now, float sx, float sy, float zoom) {
  static const int DIR_MAP[8] = {0, 1, 2, 3, 2, 1, 0, 7};
  static const char *TAILS[3] = {"short", "long", "flat"};
  int kind = p->kind < 0 || p->kind >= pet_kind_count ? 0 : p->kind;
  int palette = palette_index(p);
  // Black shadow at 16 %.
  bool sflip;
  const PetPart *shadow = resolve("p_std_sd_001_0_0", &sflip);
  if (shadow) gfx_pet(palette, shadow, sx + (-32 - shadow->rx) * zoom, sy + (16 - shadow->ry) * zoom, zoom, false, abgr(0, 41), true);
  const char *action = moving(p, now) ? "wlk" : !strcmp(p->action, "wlk") ? "std" : p->action;
  int frame = (int)(now / 125) & 3;
  int dir = DIR_MAP[p->dir & 7], index = dir == 7 ? 4 : dir;
  bool mirror = p->dir >= 4 && p->dir <= 6;
  const char *act_base = !strcmp(action, "eat") ? "std" : action;
  const char *order3[3];
  if (p->dir == 7) { order3[0] = "hd"; order3[1] = "bd"; order3[2] = "tl"; }
  else if (p->dir == 0 || p->dir == 6) { order3[0] = "bd"; order3[1] = "hd"; order3[2] = "tl"; }
  else { order3[0] = "tl"; order3[1] = "bd"; order3[2] = "hd"; }
  u32 tint = abgr(pet_kinds[kind].colors[p->color % pet_kinds[kind].color_count], 255);
  float ox = sx - 48 * zoom, oy = sy + (16 - 80 - p->z) * zoom;
  for (int k = 0; k < 3; k++) {
    const char *name = order3[k];
    const char *model = !strcmp(name, "bd") ? "round" : !strcmp(name, "hd") ? pet_kind_keys[kind] : TAILS[kind % 3];
    const char *act = "std";
    if (!strcmp(name, "bd")) act = !strcmp(act_base, "slp") ? "lay" : act_base;
    else if (!strcmp(name, "hd") && (!strcmp(act_base, "slp") || !strcmp(act_base, "beg"))) act = act_base;
    else if (!strcmp(name, "hd") && !strcmp(action, "eat")) act = "eat";
    int f = 0;
    if (!strcmp(name, "bd") && (!strcmp(act_base, "wlk") || !strcmp(act_base, "jmp") || !strcmp(act_base, "scr"))) f = frame;
    else if (!strcmp(act, "eat")) f = frame % 2;
    char full[48];
    bool flip;
    snprintf(full, sizeof full, "p_%s_%s_%s_%d_%d", act, name, model, dir, f);
    const PetPart *part = resolve(full, &flip);
    if (!part) { snprintf(full, sizeof full, "p_std_%s_%s_%d_0", name, model, dir); part = resolve(full, &flip); }
    if (!part) continue;
    int offx = 0, offy = 0;
    if (strcmp(name, "bd")) {
      const char *group = kind == 2 ? "croco.large" : "dogcat.large";
      char key[48];
      snprintf(key, sizeof key, "%s/%s_%s_%d", group, name, act_base, frame);
      const PetOffset *o = find_offset(key);
      if (!o) { snprintf(key, sizeof key, "%s/%s_%s", group, name, act_base); o = find_offset(key); }
      if (!o) { snprintf(key, sizeof key, "%s/%s_std", group, name); o = find_offset(key); }
      if (o) { offx = o->x[index]; offy = o->y[index]; }
    }
    int rx = flip ? part->w - part->rx : part->rx;
    float px = 16 - rx + offx, py = 80 - part->ry + offy;
    bool f2 = flip;
    if (mirror) { px = 96 - px - part->w; f2 = !f2; }
    gfx_pet(palette, part, ox + px * zoom, oy + py * zoom, zoom, f2, tint, false);
  }
}
