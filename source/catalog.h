// Queries about the catalogue and the furniture exported from the web client.
#pragma once
#include "assets.h"
_Static_assert(sizeof(CatOffer)==(sizeof(void*)==8?40:24),"Club restrictions must retain the catalogue memory budget");
enum { FEATURE_STACK_MAGIC=14, FEATURE_WALK_MAGIC=15 };
static inline u8 def_height_helper(const FurniDef *d){return !d?0:d->feature==FEATURE_STACK_MAGIC?1:d->feature==FEATURE_WALK_MAGIC?2:0;}

const FurniDef *def_get(u16 id);
/** Walkable furniture the room owner switches (dance floors, disco balls; server room.rs use_item). */
bool floor_switch(u16 def);
/** Closest available graphic rotation (catalogue.ts: normalizeRotation). */
u8 def_rotation(u16 id, u8 rot);
/** Width and depth in tiles for the rotation. */
void def_size(u16 id, u8 rot, int *w, int *h);
/** Drawable variant; falls back to state 0 when the state was not exported. */
const FurniVariant *variant_get(u16 def, u8 rot, u8 state);
const FurniVariant *variant_frame(u16 def, u8 rot, u8 state, u16 frame);
/** Animation frame (furniFrame in furni.ts). */
u16 furni_frame(u16 def, u8 state, u64 time);
/** Wall-item height range from the catalogue (hotel tools/import-wall-bounds.py), the one the
 *  server applies. Art taller than the wall has a single value: standing on the floor. */
bool furni_wall_range(u16 def, int *min, int *max);
