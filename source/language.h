#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "assets.h"

enum TwLanguage { TW_EN, TW_ES, TW_ZH, TW_JA, TW_FR, TW_PT_BR, TW_LANGUAGE_COUNT };
void tw_language_init(void);
void tw_language_request(int language);
void tw_language_update(void);
int tw_language(void);
const char *tw_language_code(void);
const char *tw_language_name(int language);
const char *tw_language_error(void);
const char *tw_reception_language(void);
const char *tw_reception_slug(void);
const char *tw_translate(const char *source);
#define TR(source) tw_translate(source)
const FontGlyph *tw_cjk_glyph(unsigned code);
extern const FontGlyph font_zh_glyphs[], font_ja_glyphs[];
extern const int font_zh_count, font_ja_count;
/* Called outside a frame; failure preserves the previous font. */
bool gfx_language_font(int language);
void gfx_cjk_sprite(int sx, int sy, int sw, int sh, float x, float y, unsigned color);
