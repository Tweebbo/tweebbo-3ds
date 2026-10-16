// Classic interface look (like the web edition): steel-blue windows,
// light-grey content, capsule buttons and a dark bottom bar, drawn
// with the classic graphics from client/public/classic.
#pragma once
#include "common.h"

// Palette (0xRRGGBB) taken from the classic graphics and the web stylesheet.
// Darker classic blue keeps white 11 px titles readable on the lower LCD.
#define SK_WINDOW 0x4f7486
#define SK_CONTENT 0xefefef
#define SK_INK 0x000000
#define SK_TEXT 0x222222
#define SK_DIM 0x6b6b6b
#define SK_WHITE 0xffffff
#define SK_ROW 0xffffff
#define SK_ROW_ALT 0xf6f6f6
#define SK_SELECT 0xcfe3ec
#define SK_LINE 0xc9c9c9
#define SK_GO 0x3c7a2c
#define SK_GO_OFF 0x7b7b7b
#define SK_BAR 0x3a3a3a
#define SK_ACCENT 0xffd34d
#define SK_DANGER 0xb33a2c

u32 sk_rgb(u32 rgb);
u32 sk_rgba(u32 rgb, u8 a);

/** Touch input of the frame (filled in by ui.c). */
typedef struct {
  bool tap;        /* the stylus just touched */
  bool held;       /* still pressed */
  bool released;   /* just lifted without dragging: a "click" */
  int x, y;        /* current position */
  int x0, y0;      /* where the touch started */
  int dy;          /* vertical movement since the previous frame */
  bool dragging;   /* moved more than a few pixels: not a click */
} Touch;
extern Touch T;

bool sk_hit(float x, float y, float w, float h);
/** Copies src (NULL = empty) without cutting a UTF-8 character in half. */
void utf8_copy(char *dst, size_t cap, const char *src);
/** Like sk_hit but using the point where the touch started (for scrolling lists). */
bool sk_click(float x, float y, float w, float h);

/** Draws a classic graphic by name (e.g. "building_icon"). Returns false if it does not exist. */
bool sk_sprite(const char *name, float x, float y, float scale);
bool sk_sprite_size(const char *name, int *w, int *h);
/** Like sk_sprite at 1:1, multiplied by a color (grey graphics). */
bool sk_sprite_tint(const char *name, float x, float y, u32 rgb);

/** Window with a title and close button; returns true when close was pressed. */
bool sk_window(float x, float y, float w, float h, const char *title);
/** Status window whose explicit actions replace the close button. */
void sk_status_window(float x, float y, float w, float h, const char *title);
/** Light-grey content panel with rounded corners. */
void sk_content(float x, float y, float w, float h);
/** Capsule button (button.b). */
bool sk_button(float x, float y, float w, const char *label, bool enabled);
/** Large button (button.a) for main actions. */
bool sk_button_big(float x, float y, float w, const char *label, bool enabled);
/** Green/grey "Go" button of the navigator. */
bool sk_go(float x, float y, float w, float h, const char *label, bool active);
/** Classic text field that opens the system keyboard when touched. */
bool sk_field(float x, float y, float w, const char *text, const char *placeholder);
/** Tab (selected or not). */
bool sk_tab(float x, float y, float w, float h, const char *label, bool on);
/** Text cut with "…" to fit the width. */
float sk_text(const char *s, float x, float y, float size, u32 rgb, int align, float max_w);
/** Multi-line text within `w`; returns the height used. */
int sk_paragraph_page(const char *s, int offset, float x, float y, float w, float size, u32 rgb, int max_lines);
float sk_paragraph(const char *s, float x, float y, float w, float size, u32 rgb, int max_lines);

/** List scrolled with the stylus (dragging) or the D-Pad. */
typedef struct {
  float scroll;       /* scrolled pixels */
  int focus;          /* row focused by the D-Pad (-1 none) */
} ListState;
/** Prepares a list of `n` rows of `row_h` in the given area; returns the first visible row. */
int sk_list_begin(ListState *ls, float x, float y, float w, float h, int n, float row_h);
void sk_list_end(void);
/** y coordinate of a row (already scrolled). */
float sk_list_row_y(int i);
/** Was row i clicked? */
bool sk_list_row_clicked(int i);
void sk_scrollbar(void);

void sk_icon_coin(float x, float y);
/** Draws an avatar head (for lists of people). */
void sk_head(u64 look, float cx, float cy);
