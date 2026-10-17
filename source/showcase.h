// Showcase mode (TWEEBBO_SHOWCASE): poster build for photos on real hardware.
// It plays online like the normal client and, once inside the shared room, rotates
// through the brand character models, greets and walks a little.
#pragma once
#include "common.h"

void showcase_start(u64 now);
/** Cycles the cast and gestures once the room is online (no-op otherwise). */
void showcase_update(u64 now);
/** Banner on the top screen; call after scene_draw (text_begin already ran). */
void showcase_top(u64 now);
