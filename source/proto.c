#include "proto.h"
#include <string.h>

void w_begin(Writer *w, u8 kind) { w_begin_into(w, kind, w->data, (int)sizeof w->data); }
void w_begin_into(Writer *w, u8 kind, u8 *buf, int cap) { w->buf = buf; w->cap = cap; w->len = 0; w_u8(w, kind); }
void w_u8(Writer *w, u32 v) { if (w->len < w->cap) w->buf[w->len++] = (u8)v; }
void w_u16(Writer *w, u32 v) { w_u8(w, v & 255); w_u8(w, (v >> 8) & 255); }
void w_var(Writer *w, u64 v) {
  while (v >= 0x80) { w_u8(w, (u32)(v & 0x7f) | 0x80); v >>= 7; }
  w_u8(w, (u32)v);
}
void w_str(Writer *w, const char *s) {
  int n = (int)strlen(s);
  w_var(w, (u64)n);
  for (int i = 0; i < n; i++) w_u8(w, (u8)s[i]);
}

void r_init(Reader *r, const u8 *b, int len) { r->b = b; r->len = len; r->p = 0; r->err = false; }
u8 r_u8(Reader *r) {
  if (r->p >= r->len) { r->err = true; return 0; }
  return r->b[r->p++];
}
u16 r_u16(Reader *r) { u16 a = r_u8(r); return (u16)(a | (r_u8(r) << 8)); }
u64 r_var(Reader *r) {
  u64 v = 0; int shift = 0; u8 x;
  do { x = r_u8(r); v |= (u64)(x & 0x7f) << shift; shift += 7; } while ((x & 0x80) && shift < 42 && !r->err);
  return v;
}
void r_str(Reader *r, char *out, int cap) {
  int n = (int)r_var(r);
  if (n < 0 || r->p + n > r->len) { r->err = true; n = 0; }
  int keep = n < cap - 1 ? n : cap - 1;
  while (keep > 0 && keep < n && (r->b[r->p + keep] & 0xC0) == 0x80) keep--;
  memcpy(out, r->b + r->p, (size_t)keep);
  out[keep] = 0;
  r->p += n;
}
