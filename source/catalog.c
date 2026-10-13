#include <string.h>
#include "catalog.h"

const FurniDef *def_get(u16 id) {
  int lo = 0, hi = furni_def_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (furni_defs[mid].id == id) return &furni_defs[mid];
    if (furni_defs[mid].id < id) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

u8 def_rotation(u16 id, u8 rot) {
  const FurniDef *d = def_get(id);
  rot &= 3;
  if (!d || !d->rotations || (d->rotations >> rot & 1)) return rot;
  for (int r = rot; r < 4; r++) if (d->rotations >> r & 1) return (u8)r;
  for (int r = 0; r < 4; r++) if (d->rotations >> r & 1) return (u8)r;
  return rot;
}

void def_size(u16 id, u8 rot, int *w, int *h) {
  const FurniDef *d = def_get(id);
  if (!d) { *w = *h = 1; return; }
  if (def_rotation(id, rot) % 2) { *w = d->h; *h = d->w; } else { *w = d->w; *h = d->h; }
}

static const FurniVariant *find(u16 def, u8 rot, u8 state, u8 frame) {
  int lo = 0, hi = furni_variant_count - 1;
  u64 key = (u64)def << 24 | (u64)rot << 16 | (u64)state << 8 | frame;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const FurniVariant *v = &furni_variants[mid];
    u64 k = (u64)v->def << 24 | (u64)v->rot << 16 | (u64)v->state << 8 | v->frame;
    if (k == key) return v;
    if (k < key) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

const FurniVariant *variant_get(u16 def, u8 rot, u8 state) { return variant_frame(def, rot, state, 0); }

const FurniVariant *variant_frame(u16 def, u8 rot, u8 state, u8 frame) {
  // Only the graphic rotations that exist are exported (normalizeRotation on the web).
  rot = def_rotation(def, rot);
  const FurniVariant *v = find(def, rot, state, frame);
  if (!v) v = find(def, rot, state, 0);
  if (!v) v = find(def, rot, 0, 0);
  if (!v) {
    // One line per item at most: this runs every frame.
    static u16 logged[32];
    static int n;
    bool seen = false;
    for (int i = 0; i < n; i++) seen |= logged[i] == def;
    if (!seen && n < 32) { logged[n++] = def; dbg_log("no graphics for furniture %u rot %u state %u", def, rot, state); }
  }
  return v;
}

u8 furni_frame(u16 def, u8 state, u64 time) {
  const FurniDef *d = def_get(def);
  if (!d) return 0;
  // Same as furniFrame() in furni.ts.
  if (state > 0 && (d->flags & DEF_HOLO)) return (u8)((time / 100) % 32);
  if (state == 2 && (d->flags & DEF_TELEPORTER)) return (u8)((time / 100) % 2);
  if (def == 8 && state == 1) return (u8)((time / 160) % 8);
  if ((def == 7 || d->kind == KIND_DICE) && state == 7) return (u8)((time / 90) % 6);
  return 0;
}
