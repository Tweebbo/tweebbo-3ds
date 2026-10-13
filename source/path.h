// 8-direction A* without cutting corners (server/src/pathfind.rs).
#pragma once
#include "common.h"

/** Protocol directions: 0 = -y, 1 = +x-y, 2 = +x, … 7 = -x-y. */
extern const s8 DIRS[8][2];
u8 dir_of(int dx, int dy);

typedef bool (*WalkFn)(void *ctx, int x, int y, bool goal);
/** Writes the path without the starting tile. Returns its length or -1. */
int path_find(int w, int h, int sx, int sy, int gx, int gy, WalkFn walk, void *ctx, u8 (*out)[2], int cap);
