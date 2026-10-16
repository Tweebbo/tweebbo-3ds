// On-screen keyboard drawn on the touch screen. Unlike the system keyboard applet it does not
// suspend the app, so the room (or a Snow Storm match) keeps moving on the top screen while you type.
#include "osk.h"
#include "skin.h"
#include "gfx.h"
#include "ui.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#define KEY_W 32
#define ROW_H 34
#define TOP 54

static struct {
  bool active, shift, caps, secret;
  int page; /* 0 letters, 1 symbols and accents */
  int fx, fy; /* D-Pad focus */
  int max_chars;
  char title[64], text[512];
  OskDone done;
  void *ctx;
  u64 repeat_at;
} O;

static const char *const LETTERS[4][10] = {
  {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
  {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
  {"a", "s", "d", "f", "g", "h", "j", "k", "l", "'"},
  {"z", "x", "c", "v", "b", "n", "m", ",", ".", "?"},
};
static const char *const SYMBOLS[4][10] = {
  {"!", "@", "#", "&", "*", "(", ")", "-", "_", "+"},
  {"á", "é", "í", "ó", "ú", "ñ", "ü", "ç", "¿", "¡"},
  {"Á", "É", "Í", "Ó", "Ú", "Ñ", ":", ";", "\"", "/"},
  {"=", "%", "$", "<", ">", "[", "]", "~", "^", "|"},
};
enum { K_SHIFT, K_PAGE, K_SPACE, K_BACK, K_SEND };
static const float BOTTOM_X[5] = {0, 50, 100, 220, 270}, BOTTOM_W[5] = {48, 48, 118, 48, 50};

static int chars(const char *s) { int n = 0; for (; *s; s++) if (((u8)*s & 0xC0) != 0x80) n++; return n; }

static const char *letter(int row, int col, char *buf) {
  const char *k = O.page ? SYMBOLS[row][col] : LETTERS[row][col];
  if (!O.page && (O.shift || O.caps) && k[0] >= 'a' && k[0] <= 'z' && !k[1]) { buf[0] = (char)(k[0] - 32); buf[1] = 0; return buf; }
  return k;
}

static void type(const char *s) {
  if (chars(O.text) >= O.max_chars || strlen(O.text) + strlen(s) >= sizeof O.text) return;
  strcat(O.text, s);
  if (O.shift && !O.caps) O.shift = false;
}

static void backspace(void) {
  size_t n = strlen(O.text);
  while (n > 0 && ((u8)O.text[n - 1] & 0xC0) == 0x80) n--;
  if (n > 0) n--;
  O.text[n] = 0;
}

static void finish(bool send) {
  O.active = false;
  OskDone done = O.done;
  void *ctx = O.ctx;
  char text[512];
  snprintf(text, sizeof text, "%s", O.text);
  memset(O.text, 0, sizeof O.text);
  // Blank messages are not sent.
  bool blank = true;
  for (const char *p = text; *p; p++) if (*p != ' ') blank = false;
  if (send && !blank && done) done(text, ctx);
}

void osk_open(const char *title, const char *initial, int max_chars, OskDone done, void *ctx) {
  memset(&O, 0, sizeof O);
  O.active = true;
  O.max_chars = max_chars > 0 ? max_chars : 100;
  snprintf(O.title, sizeof O.title, "%s", title);
  if (initial) snprintf(O.text, sizeof O.text, "%s", initial);
  O.done = done;
  O.ctx = ctx;
  O.fx = 0; O.fy = 1;
}

void osk_open_secret(const char *title, int max_chars, OskDone done, void *ctx) {
  osk_open(title, NULL, max_chars, done, ctx);
  O.secret = true;
}

bool osk_active(void) { return O.active; }
void osk_cancel(void) { if (O.active) finish(false); }

bool osk_input(u32 down, u32 held, u64 now) {
  if (!O.active) return false;
  if (down & KEY_DUP) O.fy = O.fy > 0 ? O.fy - 1 : 4;
  if (down & KEY_DDOWN) O.fy = O.fy < 4 ? O.fy + 1 : 0;
  int cols = O.fy == 4 ? 5 : 10;
  if (O.fx >= cols) O.fx = cols - 1;
  if (down & KEY_DLEFT) O.fx = O.fx > 0 ? O.fx - 1 : cols - 1;
  if (down & KEY_DRIGHT) O.fx = O.fx < cols - 1 ? O.fx + 1 : 0;
  if (down & KEY_A) {
    char buf[8];
    if (O.fy < 4) type(letter(O.fy, O.fx, buf));
    else if (O.fx == K_SHIFT) O.shift = !O.shift;
    else if (O.fx == K_PAGE) O.page ^= 1;
    else if (O.fx == K_SPACE) type(" ");
    else if (O.fx == K_BACK) backspace();
    else { finish(true); return true; }
  }
  if (down & KEY_X) O.shift = !O.shift;
  if (down & KEY_Y) type(" ");
  if (down & KEY_R) O.page ^= 1;
  if (down & KEY_START) { finish(true); return true; }
  if (down & KEY_B) {
    if (!O.text[0]) { finish(false); return true; }
    backspace();
    O.repeat_at = now + 450;
  } else if ((held & KEY_B) && O.repeat_at && now >= O.repeat_at) { backspace(); O.repeat_at = now + 70; }
  if (!(held & KEY_B)) O.repeat_at = 0;
  return true;
}

static bool key(float x, float y, float w, const char *label, bool focus, bool on) {
  bool down = T.held && !T.dragging && T.x0 >= x && T.y0 >= y && T.x0 < x + w && T.y0 < y + ROW_H - 2;
  fill_round(x + 1, y + 1, w - 2, ROW_H - 3, 4, sk_rgb(focus ? 0xffd34d : 0x6b7880));
  fill_round(x + 2, y + 2, w - 4, ROW_H - 5, 3, sk_rgb(down ? 0xc9d3d8 : on ? 0xdff0ff : 0xf4f4f0));
  float size = text_width(label, 12) > w - 6 ? 9.5f : 12;
  text_draw(label, x + w / 2, y + (ROW_H - 2) / 2 - size / 2 - 1, size, sk_rgb(0x1d2a33), 1);
  return T.released && T.x0 >= x && T.y0 >= y && T.x0 < x + w && T.y0 < y + ROW_H - 2 &&
         T.x >= x && T.y >= y && T.x < x + w && T.y < y + ROW_H - 2;
}

bool osk_draw(u64 now) {
  (void)now;
  if (!O.active) return false;
  fill_rect(0, 0, BOT_W, BOT_H, sk_rgb(0x1f2a30));
  sk_text(O.title, 6, 3, 10.5f, SK_WHITE, 0, 230);
  char count[16];
  snprintf(count, sizeof count, "%d/%d", chars(O.text), O.max_chars);
  sk_text(count, BOT_W - 30, 4, 9, 0x8a959c, 2, 60);
  // Close (no send).
  fill_round(BOT_W - 24, 2, 20, 16, 3, sk_rgb(0x6b7880));
  text_draw("x", BOT_W - 14, 2, 11, sk_rgb(SK_WHITE), 1);
  if (T.released && T.x0 >= BOT_W - 26 && T.y0 < 20) { finish(false); return true; }
  // Text field: the end of the text stays visible.
  fill_round(4, 22, BOT_W - 8, 26, 4, sk_rgb(0x7d7d7d));
  fill_round(5, 23, BOT_W - 10, 24, 3, sk_rgb(SK_WHITE));
  char stars[129];
  if (O.secret) {
    int n = chars(O.text);
    if (n > 128) n = 128;
    memset(stars, '*', (size_t)n); stars[n] = 0;
  }
  const char *shown = O.secret ? stars : O.text;
  while (*shown && text_width(shown, 12) > BOT_W - 26) { shown++; while (((u8)*shown & 0xC0) == 0x80) shown++; }
  float tw = text_draw(shown, 10, 28, 12, sk_rgb(0x1d2a33), 0);
  if ((osGetTime() / 500) % 2) fill_rect(11 + tw, 27, 1, 16, sk_rgb(0x1d2a33));
  if (!O.text[0]) text_draw("Type with the keys or the buttons", 12, 29, 10, sk_rgb(0x9a9a9a), 0);
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 10; c++) {
      char buf[8];
      const char *k = letter(r, c, buf);
      if (key(c * KEY_W, TOP + r * ROW_H, KEY_W, k, O.fy == r && O.fx == c, false)) { type(k); O.fy = r; O.fx = c; }
    }
  const char *labels[5] = {O.caps ? "CAPS" : "Shift X", O.page ? "abc R" : "áé# R", "Space Y", "Del B", "Send"};
  float y = TOP + 4 * ROW_H;
  for (int i = 0; i < 5; i++) {
    bool on = (i == K_SHIFT && (O.shift || O.caps)) || (i == K_PAGE && O.page);
    if (!key(BOTTOM_X[i], y, BOTTOM_W[i], labels[i], O.fy == 4 && O.fx == i, on)) continue;
    O.fy = 4; O.fx = i;
    // Shift: once for the next letter, twice for caps lock, a third time to turn it off.
    if (i == K_SHIFT) { if (O.caps) O.caps = O.shift = false; else if (O.shift) O.caps = true; else O.shift = true; }
    else if (i == K_PAGE) O.page ^= 1;
    else if (i == K_SPACE) type(" ");
    else if (i == K_BACK) backspace();
    else { finish(true); return true; }
  }
  return true;
}
