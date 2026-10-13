// Pets in the room (PETS message), drawn like in the web edition.
#pragma once
#include "common.h"

#define MAX_PETS 16

typedef struct {
  int id, kind, breed, color, x, y, dir, z;
  u32 owner;
  char name[24], action[8];
  float fx, fy;
  u64 t0;
} Pet;

/** Re-reads W.pets_json when it changed. */
void pets_sync(u64 now);
int pets_count(void);
const Pet *pet_at(int i);
void pet_pos(const Pet *p, u64 now, float *x, float *y);
/** (sx, sy): screen point of the tile (top vertex), zoom already applied. */
void pet_draw(const Pet *p, u64 now, float sx, float sy, float zoom);
