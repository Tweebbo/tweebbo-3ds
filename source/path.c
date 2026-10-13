#include "path.h"
#include <stdlib.h>
#include <string.h>

const s8 DIRS[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

static int sgn(int v) { return (v > 0) - (v < 0); }

u8 dir_of(int dx, int dy) {
  for (int i = 0; i < 8; i++) if (DIRS[i][0] == sgn(dx) && DIRS[i][1] == sgn(dy)) return (u8)i;
  return 2;
}

#define SIDE 64
typedef struct { u32 f; u16 i; } Node;

static void push(Node *heap, int *n, Node v) {
  int i = (*n)++;
  while (i > 0) {
    int p = (i - 1) / 2;
    if (heap[p].f < v.f || (heap[p].f == v.f && heap[p].i <= v.i)) break;
    heap[i] = heap[p]; i = p;
  }
  heap[i] = v;
}

static Node pop(Node *heap, int *n) {
  Node top = heap[0], last = heap[--(*n)];
  int i = 0;
  for (;;) {
    int c = i * 2 + 1;
    if (c >= *n) break;
    if (c + 1 < *n && (heap[c + 1].f < heap[c].f || (heap[c + 1].f == heap[c].f && heap[c + 1].i < heap[c].i))) c++;
    if (last.f < heap[c].f || (last.f == heap[c].f && last.i <= heap[c].i)) break;
    heap[i] = heap[c]; i = c;
  }
  heap[i] = last;
  return top;
}

int path_find(int w, int h, int sx, int sy, int gx, int gy, WalkFn walk, void *ctx, u8 (*out)[2], int cap) {
  static u32 g[SIDE * SIDE];
  static u16 came[SIDE * SIDE];
  static u8 closed[SIDE * SIDE];
  static Node heap[SIDE * SIDE * 8];
  if (w > SIDE || h > SIDE || gx < 0 || gy < 0 || gx >= w || gy >= h) return -1;
  int n = w * h, s = sy * w + sx, t = gy * w + gx, open = 0;
  // The door may lie outside the grid (threshold): it is treated as a valid origin.
  if (sx < 0 || sy < 0 || sx >= w || sy >= h) return -1;
  memset(g, 0xff, sizeof(u32) * n);
  memset(closed, 0, (size_t)n);
  g[s] = 0;
  #define HEUR(i) ({ int dx = abs((i) % w - gx), dy = abs((i) / w - gy); (u32)(10 * (dx + dy) - 6 * (dx < dy ? dx : dy)); })
  push(heap, &open, (Node){HEUR(s), (u16)s});
  while (open) {
    Node cur = pop(heap, &open);
    if (cur.i == t) {
      int len = 0;
      for (int c = t; c != s; c = came[c]) len++;
      if (len > cap) return -1;
      int k = len;
      for (int c = t; c != s; c = came[c]) { k--; out[k][0] = (u8)(c % w); out[k][1] = (u8)(c / w); }
      return len;
    }
    if (closed[cur.i]) continue;
    closed[cur.i] = 1;
    int cx = cur.i % w, cy = cur.i / w;
    for (int d = 0; d < 8; d++) {
      int nx = cx + DIRS[d][0], ny = cy + DIRS[d][1];
      if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
      int ni = ny * w + nx;
      if (closed[ni] || !walk(ctx, nx, ny, ni == t)) continue;
      bool diagonal = DIRS[d][0] && DIRS[d][1];
      if (diagonal && (!walk(ctx, nx, cy, false) || !walk(ctx, cx, ny, false))) continue;
      u32 ng = g[cur.i] + (diagonal ? 14 : 10);
      if (ng < g[ni]) {
        g[ni] = ng; came[ni] = (u16)cur.i;
        if (open < SIDE * SIDE * 8) push(heap, &open, (Node){ng + HEUR(ni), (u16)ni});
      }
    }
  }
  #undef HEUR
  return -1;
}
