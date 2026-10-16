#include "skin.h"
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "avatar.h"
#include "gfx.h"

Touch T;

u32 sk_rgb(u32 rgb) { return C2D_Color32(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, 255); }
u32 sk_rgba(u32 rgb, u8 a) { return C2D_Color32(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, a); }

bool sk_hit(float x, float y, float w, float h) {
  return T.released && T.x >= x && T.y >= y && T.x < x + w && T.y < y + h && T.x0 >= x && T.y0 >= y && T.x0 < x + w && T.y0 < y + h;
}

void utf8_copy(char *dst, size_t cap, const char *src) {
  if (!cap) return;
  size_t n = src ? strlen(src) : 0;
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((u8)src[n] & 0xC0) == 0x80) n--;
  }
  if (n) memcpy(dst, src, n);
  dst[n] = 0;
}

bool sk_click(float x, float y, float w, float h) { return sk_hit(x, y, w, h); }

static bool pressed_on(float x, float y, float w, float h) {
  return T.held && !T.dragging && T.x0 >= x && T.y0 >= y && T.x0 < x + w && T.y0 < y + h;
}

static const UiSprite *find(const char *name) {
  int lo = 0, hi = ui_sprite_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(ui_sprites[mid].name, name);
    if (!c) return &ui_sprites[mid];
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

bool sk_sprite_size(const char *name, int *w, int *h) {
  const UiSprite *s = find(name);
  if (!s) return false;
  *w = s->w; *h = s->h;
  return true;
}

bool sk_sprite_tint(const char *name, float x, float y, u32 rgb) {
  const UiSprite *s = find(name);
  if (!s) return false;
  gfx_sprite(s->page, s->x, s->y, s->w, s->h, floorf(x), floorf(y), 1, false,
             0xFF000000 | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff));
  return true;
}

bool sk_sprite(const char *name, float x, float y, float scale) {
  const UiSprite *s = find(name);
  if (!s) return false;
  float dx = x - s->rx * scale, dy = y - s->ry * scale;
  if (scale == 1) { dx = floorf(dx); dy = floorf(dy); }
  gfx_sprite(s->page, s->x, s->y, s->w, s->h, dx, dy, scale, false, 0xFFFFFFFF);
  return true;
}

static bool window(float x, float y, float w, float h, const char *title, bool closable) {
  // Steel-blue frame with a rounded black border, like window.* on the web.
  fill_round(x, y, w, h, 6, sk_rgb(SK_INK));
  fill_round(x + 1, y + 1, w - 2, h - 2, 5, sk_rgb(SK_WINDOW));
  bool close = false;
  if (title) {
    sk_text(title, x + w / 2, y + 3, 11, SK_WHITE, 1, w - 40);
    if (!closable) return false;
    float cx = x + w - 17, cy = y + 3;
    close = sk_hit(cx - 6, cy - 4, 25, 21);
    if (!sk_sprite(pressed_on(cx - 6, cy - 4, 25, 21) ? "button.close.down" : "button.close.up", cx, cy, 1))
      text_draw("x", cx + 6, cy, 11, sk_rgb(SK_WHITE), 1);
  }
  return close;
}

bool sk_window(float x, float y, float w, float h, const char *title) { return window(x, y, w, h, title, true); }
void sk_status_window(float x, float y, float w, float h, const char *title) { window(x, y, w, h, title, false); }

void sk_content(float x, float y, float w, float h) {
  fill_round(x, y, w, h, 4, sk_rgba(0x000000, 60));
  fill_round(x, y, w - 1, h - 1, 4, sk_rgb(SK_CONTENT));
}

static float label_size(const char *label, float w, float size) {
  while (size > 10 && text_width(label, size) > w - 10) size -= 0.5f;
  return size;
}

bool sk_button(float x, float y, float w, const char *label, bool enabled) {
  const float h = 18;
  bool down = enabled && pressed_on(x, y, w, h);
  fill_round(x, y, w, h, h / 2, sk_rgb(enabled ? SK_INK : 0x9a9a9a));
  fill_round(x + 1, y + 1, w - 2, h - 2, (h - 2) / 2, sk_rgb(down ? 0xd8d8d8 : SK_WHITE));
  if (!down) fill_rect(x + 5, y + h - 5, w - 10, 2, sk_rgb(0xdedede));
  float size = label_size(label, w, 10.5f);
  sk_text(label, x + w / 2, y + (h - size) / 2 - 3, size, enabled ? SK_TEXT : 0x9a9a9a, 1, w - 8);
  return enabled && sk_hit(x, y, w, h);
}

bool sk_button_big(float x, float y, float w, const char *label, bool enabled) {
  const float h = 23;
  bool down = enabled && pressed_on(x, y, w, h);
  fill_round(x, y, w, h, 8, sk_rgb(enabled ? SK_INK : 0x9a9a9a));
  fill_round(x + 2, y + 2, w - 4, h - 4, 6, sk_rgb(down ? 0xd0d0d0 : SK_WHITE));
  if (!down) fill_rect(x + 6, y + h - 7, w - 12, 3, sk_rgb(0xe2e2e2));
  float size = label_size(label, w, 11.5f);
  float tw = sk_text(label, x + w / 2, y + (h - size) / 2 - 3, size, enabled ? SK_TEXT : 0x9a9a9a, 1, w - 8);
  (void)tw;
  return enabled && sk_hit(x, y, w, h);
}

bool sk_go(float x, float y, float w, float h, const char *label, bool active) {
  bool down = pressed_on(x, y, w, h);
  fill_round(x, y, w, h, 3, sk_rgb(SK_INK));
  fill_round(x + 1, y + 1, w - 2, h - 2, 2, sk_rgb(down ? 0x2a2a2a : active ? SK_GO : SK_GO_OFF));
  text_draw(label, x + w / 2, y + (h - 12) / 2, 10, sk_rgb(SK_WHITE), 1);
  return sk_hit(x, y, w, h);
}

bool sk_field(float x, float y, float w, const char *text, const char *placeholder) {
  const float h = 18;
  fill_round(x, y, w, h, 3, sk_rgb(0x7d7d7d));
  fill_round(x + 1, y + 1, w - 2, h - 2, 2, sk_rgb(SK_WHITE));
  bool empty = !text || !text[0];
  sk_text(empty ? placeholder : text, x + 5, y + 3, 10.5f, empty ? 0x9a9a9a : SK_TEXT, 0, w - 10);
  return sk_hit(x, y, w, h);
}

bool sk_tab(float x, float y, float w, float h, const char *label, bool on) {
  fill_round(x, y, w, h + 4, 4, sk_rgb(0x7f7f7f));
  fill_round(x + 1, y + 1, w - 2, h + 2, 3, sk_rgb(on ? 0xf5f5ef : 0xd6d6cf));
  float size = label_size(label, w, 10.5f);
  text_draw(label, x + w / 2, y + (h - size) / 2 - 3, size, sk_rgb(SK_TEXT), 1);
  return sk_hit(x, y, w, h);
}

float sk_text(const char *s, float x, float y, float size, u32 rgb, int align, float max_w) {
  char buf[256];
  utf8_copy(buf, sizeof buf, s);
  if (max_w > 0) text_fit(buf, size, max_w);
  return text_draw(buf, x, y, size, sk_rgb(rgb), align);
}

static int paragraph(const char *s, int offset, float x, float y, float w, float size, u32 rgb, int max_lines, int *lines) {
  char line[256];
  int length = (int)strlen(s);
  if (offset < 0) offset = 0;
  if (offset > length) offset = length;
  const char *p = s + offset;
  *lines = 0;
  while (*p && *lines < max_lines) {
    int n = 0, last_space = -1;
    while (p[n] && p[n] != '\n') {
      int next = n + 1;
      while (p[next] && ((u8)p[next] & 0xc0) == 0x80) next++;
      if (next >= (int)sizeof line) break;
      memcpy(line, p, (size_t)next); line[next] = 0;
      if (text_width(line, size) > w) break;
      if (p[n] == ' ') last_space = n;
      n = next;
    }
    int take = n;
    if (p[n] && p[n] != '\n' && last_space > 0) take = last_space;
    if (!take && *p != '\n') {
      take = 1;
      while (p[take] && ((u8)p[take] & 0xc0) == 0x80) take++;
    }
    memcpy(line, p, (size_t)take); line[take] = 0;
    text_draw(line, x, y + *lines * ceilf(size * 1.3f), size, sk_rgb(rgb), 0);
    (*lines)++;
    p += take;
    while (*p == ' ') p++;
    if (*p == '\n') p++;
  }
  return (int)(p - s);
}

int sk_paragraph_page(const char *s, int offset, float x, float y, float w, float size, u32 rgb, int max_lines) {
  int lines;
  return paragraph(s, offset, x, y, w, size, rgb, max_lines, &lines);
}

float sk_paragraph(const char *s, float x, float y, float w, float size, u32 rgb, int max_lines) {
  int lines;
  paragraph(s, 0, x, y, w, size, rgb, max_lines, &lines);
  return lines * ceilf(size * 1.3f);
}

// ---------------------------------------------------------------- lists

static struct {
  ListState *ls;
  float x, y, w, h, row_h, content;
  int n;
} L;

static void scissor(float x, float y, float w, float h) {
  C2D_Flush();
  // The bottom screen is rotated in the framebuffer (240×320).
  C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)fmaxf(0, 240 - (y + h)), (u32)fmaxf(0, 320 - (x + w)), (u32)fminf(240, 240 - y),
                 (u32)fminf(320, 320 - x));
}

static void scissor_off(void) {
  C2D_Flush();
  C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

int sk_list_begin(ListState *ls, float x, float y, float w, float h, int n, float row_h) {
  L.ls = ls; L.x = x; L.y = y; L.w = w; L.h = h; L.n = n; L.row_h = row_h; L.content = n * row_h;
  bool inside = T.x0 >= x && T.x0 < x + w && T.y0 >= y && T.y0 < y + h;
  if (T.held && T.dragging && inside) ls->scroll -= (float)T.dy;
  if (ls->focus >= 0 && ls->focus < n) {
    // The D-Pad keeps the focused row visible.
    float top = ls->focus * row_h;
    if (top < ls->scroll) ls->scroll = top;
    if (top + row_h > ls->scroll + h) ls->scroll = top + row_h - h;
  }
  float max = L.content - h;
  if (ls->scroll > max) ls->scroll = max;
  if (ls->scroll < 0) ls->scroll = 0;
  scissor(x, y, w, h);
  int first = (int)(ls->scroll / row_h);
  return first < 0 ? 0 : first;
}

float sk_list_row_y(int i) { return L.y + i * L.row_h - L.ls->scroll; }

bool sk_list_row_clicked(int i) {
  float ry = sk_list_row_y(i);
  if (ry + L.row_h < L.y || ry > L.y + L.h) return false;
  return T.released && T.y0 >= L.y && T.y0 < L.y + L.h && T.x0 >= L.x && T.x0 < L.x + L.w && T.y0 >= ry && T.y0 < ry + L.row_h;
}

void sk_list_end(void) {
  scissor_off();
  sk_scrollbar();
}

void sk_scrollbar(void) {
  if (L.content <= L.h) return;
  float track = L.h - 4, thumb = fmaxf(14, track * L.h / L.content);
  float t = L.ls->scroll / (L.content - L.h);
  fill_round(L.x + L.w - 5, L.y + 2, 4, track, 2, sk_rgba(0x000000, 40));
  fill_round(L.x + L.w - 5, L.y + 2 + (track - thumb) * t, 4, thumb, 2, sk_rgba(0x000000, 140));
}

void sk_icon_coin(float x, float y) {
  if (!sk_sprite("credits_icon@14", x, y, 1)) {
    C2D_DrawCircleSolid(x + 6, y + 6, 0.5f, 5, sk_rgb(0xf2b134));
  }
}

void sk_head(u64 look, float cx, float cy) {
  // The head covers roughly y 20–52 of the 64×112 canvas.
  avatar_draw_head(look, 2, cx - ANCHOR_X * 0.75f, cy - 38 * 0.75f, 0.75f);
}
