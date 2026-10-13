// Queries about the catalogue and the furniture exported from the web client.
#pragma once
#include "assets.h"

const FurniDef *def_get(u16 id);
/** Closest available graphic rotation (catalogue.ts: normalizeRotation). */
u8 def_rotation(u16 id, u8 rot);
/** Width and depth in tiles for the rotation. */
void def_size(u16 id, u8 rot, int *w, int *h);
/** Drawable variant; falls back to state 0 when the state was not exported. */
const FurniVariant *variant_get(u16 def, u8 rot, u8 state);
const FurniVariant *variant_frame(u16 def, u8 rot, u8 state, u8 frame);
/** Animation frame (furniFrame in furni.ts). */
u8 furni_frame(u16 def, u8 state, u64 time);
