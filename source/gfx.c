#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "catalog.h"

#define MAX_PAGES 10
#define MAX_ROOMS 8

static C3D_Tex pages[MAX_PAGES];
static int page_count;
static struct { const RoomBackground *bg; C3D_Tex tex; bool loaded; } rooms[MAX_ROOMS];
static C2D_TextBuf text_buf;
static bool linear;
static void scene_free(void);
static void draw_tex(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color);

// Furniture textures: one per definition, within a linear-memory budget.
#define FURNI_SLOTS 160
#define FURNI_BUDGET_MAX (16u * 1024 * 1024)
/** Adjusted in gfx_init to the free linear memory (Old 3DS has much less). */
static u32 furni_budget = 8u * 1024 * 1024;
static struct { u16 def; bool loaded, failed; C3D_Tex tex; u32 used; } furni[FURNI_SLOTS];
static u32 frame_no, furni_bytes;

static bool load_t3x(const char *path, C3D_Tex *tex) {
  FILE *f = fopen(path, "rb");
  if (!f) { dbg_log("missing %s", path); return false; }
  Tex3DS_Texture t3x = Tex3DS_TextureImportStdio(f, tex, NULL, false);
  fclose(f);
  if (!t3x) { dbg_log("could not load %s (linear free %lu KB)", path, (unsigned long)(linearSpaceFree() / 1024)); return false; }
  Tex3DS_TextureFree(t3x);
  C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  return true;
}

bool gfx_init(void) {
  char path[64];
  for (page_count = 0; page_count < atlas_page_count && page_count < MAX_PAGES; page_count++) {
    snprintf(path, sizeof path, "romfs:/gfx/%s.t3x", atlas_page_files[page_count]);
    if (!load_t3x(path, &pages[page_count])) return false;
  }
  text_buf = C2D_TextBufNew(16384);
  // The system font is drawn scaled down (≈1/3): linear filtering keeps it smooth instead of jagged.
  C2D_FontSetFilter(NULL, GPU_LINEAR, GPU_LINEAR);
  // Keep 6 MB for room backgrounds, text and citro2d buffers.
  u32 free_linear = linearSpaceFree();
  furni_budget = free_linear > 10u * 1024 * 1024 ? free_linear - 6u * 1024 * 1024 : 4u * 1024 * 1024;
  if (furni_budget > FURNI_BUDGET_MAX) furni_budget = FURNI_BUDGET_MAX;
  dbg_log("atlas pages %d, furniture budget %lu KB", page_count, (unsigned long)(furni_budget / 1024));
  // Avatar parts and colored furniture are tinted by multiplication,
  // like spriteImage() in the web client. Shapes and text are not affected.
  C2D_SetTintMode(C2D_TintMult);
  return true;
}

static int furni_slot(u16 def) {
  int free_slot = -1;
  for (int i = 0; i < FURNI_SLOTS; i++) {
    if ((furni[i].loaded || furni[i].failed) && furni[i].def == def) return i;
    if (!furni[i].loaded && !furni[i].failed && free_slot < 0) free_slot = i;
  }
  return free_slot;
}

static C3D_Tex *furni_tex(u16 def) {
  const FurniDef *d = def_get(def);
  if (!d || !d->tex) return NULL;
  int i = furni_slot(def);
  if (i < 0) {
    // No free slot: evict the least recently used texture not drawn this frame.
    for (int k = 0; k < FURNI_SLOTS; k++)
      if (furni[k].used != frame_no && (i < 0 || furni[k].used < furni[i].used)) i = k;
    if (i < 0) return NULL;
    if (furni[i].loaded) { furni_bytes -= furni[i].tex.size; C3D_TexDelete(&furni[i].tex); }
    furni[i].loaded = furni[i].failed = false;
  }
  furni[i].used = frame_no;
  if (furni[i].loaded) return &furni[i].tex;
  if (furni[i].failed) return NULL;
  char path[48];
  snprintf(path, sizeof path, "romfs:/furni/d%u.t3x", (unsigned)def);
  furni[i].def = def;
  if (!load_t3x(path, &furni[i].tex)) { furni[i].failed = true; return NULL; }
  if (linear) C3D_TexSetFilter(&furni[i].tex, GPU_LINEAR, GPU_LINEAR);
  furni[i].loaded = true;
  furni_bytes += furni[i].tex.size;
  return &furni[i].tex;
}

void gfx_furni_preload(u16 def) { furni_tex(def); }

// Pets: one 512×512 texture per breed palette; enough for a room (max. 5 pets).
#define PET_SLOTS 6
static struct { int palette; bool loaded; C3D_Tex tex; u32 used; } pet_tex[PET_SLOTS];

static C3D_Tex *pet_texture(int palette) {
  int victim = -1;
  for (int i = 0; i < PET_SLOTS; i++) {
    if (pet_tex[i].loaded && pet_tex[i].palette == palette) { pet_tex[i].used = frame_no; return &pet_tex[i].tex; }
    if (!pet_tex[i].loaded) { if (victim < 0 || pet_tex[victim].loaded) victim = i; }
    else if (victim < 0 || (pet_tex[victim].loaded && pet_tex[i].used < pet_tex[victim].used && pet_tex[i].used != frame_no)) victim = i;
  }
  if (victim < 0) return NULL;
  if (pet_tex[victim].loaded) {
    if (pet_tex[victim].used == frame_no) return NULL;
    C2D_Flush();
    C3D_TexDelete(&pet_tex[victim].tex);
    pet_tex[victim].loaded = false;
  }
  char path[48];
  snprintf(path, sizeof path, "romfs:/gfx/pets_%d.t3x", palette);
  if (!load_t3x(path, &pet_tex[victim].tex)) return NULL;
  if (linear) C3D_TexSetFilter(&pet_tex[victim].tex, GPU_LINEAR, GPU_LINEAR);
  pet_tex[victim].loaded = true;
  pet_tex[victim].palette = palette;
  pet_tex[victim].used = frame_no;
  return &pet_tex[victim].tex;
}

void gfx_pet(int palette, const PetPart *p, float x, float y, float scale, bool flip, u32 color, bool solid) {
  C3D_Tex *tex = pet_texture(palette);
  if (!tex) return;
  if (solid) { C2D_Flush(); C2D_SetTintMode(C2D_TintSolid); }
  draw_tex(tex, p->x, p->y, p->w, p->h, x, y, scale, flip, color);
  if (solid) { C2D_Flush(); C2D_SetTintMode(C2D_TintMult); }
}

void gfx_frame(void) {
  frame_no++;
  while (furni_bytes > furni_budget) {
    int victim = -1;
    for (int k = 0; k < FURNI_SLOTS; k++)
      if (furni[k].loaded && furni[k].used + 1 < frame_no && (victim < 0 || furni[k].used < furni[victim].used)) victim = k;
    if (victim < 0) break;
    furni_bytes -= furni[victim].tex.size;
    C3D_TexDelete(&furni[victim].tex);
    furni[victim].loaded = false;
  }
}

void gfx_exit(void) {
  scene_free();
  for (int i = 0; i < PET_SLOTS; i++) if (pet_tex[i].loaded) C3D_TexDelete(&pet_tex[i].tex);
  for (int i = 0; i < FURNI_SLOTS; i++) if (furni[i].loaded) C3D_TexDelete(&furni[i].tex);
  for (int i = 0; i < page_count; i++) C3D_TexDelete(&pages[i]);
  for (int i = 0; i < MAX_ROOMS; i++) if (rooms[i].loaded) C3D_TexDelete(&rooms[i].tex);
  C2D_TextBufDelete(text_buf);
}

static void draw_tex_xy(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  Tex3DS_SubTexture sub = {
    .width = (u16)sw, .height = (u16)sh,
    .left = sx / (float)tex->width, .right = (sx + sw) / (float)tex->width,
    .top = 1.0f - sy / (float)tex->height, .bottom = 1.0f - (sy + sh) / (float)tex->height,
  };
  C2D_Image img = {tex, &sub};
  C2D_ImageTint tint;
  C2D_PlainImageTint(&tint, color, 1.0f);
  C2D_DrawImageAt(img, x, y, 0.5f, &tint, flip ? -kx : kx, ky);
}

static void draw_tex(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color) {
  draw_tex_xy(tex, sx, sy, sw, sh, x, y, scale, scale, flip, color);
}

// Layered public room: its pages load on entry and are freed on exit.
#define SCENE_PAGES 4
static const Scene *scene_loaded;
static C3D_Tex scene_tex[SCENE_PAGES];
static int scene_tex_n;

static void scene_free(void) {
  for (int i = 0; i < scene_tex_n; i++) C3D_TexDelete(&scene_tex[i]);
  scene_tex_n = 0;
  scene_loaded = NULL;
}

const Scene *gfx_scene(const char *model) {
  const Scene *want = NULL;
  for (int i = 0; i < scene_count; i++) if (!strcmp(scenes[i].model, model)) want = &scenes[i];
  if (want == scene_loaded) return want;
  C2D_Flush();
  scene_free();
  if (!want) return NULL;
  for (int i = 0; i < want->pages && i < SCENE_PAGES; i++) {
    char path[64];
    snprintf(path, sizeof path, "romfs:/gfx/scene_%s_%d.t3x", want->model, i);
    if (!load_t3x(path, &scene_tex[i])) { scene_free(); return NULL; }
    if (linear) C3D_TexSetFilter(&scene_tex[i], GPU_LINEAR, GPU_LINEAR);
    scene_tex_n = i + 1;
  }
  scene_loaded = want;
  return want;
}

void gfx_scene_sprite(const SceneSprite *s, float x, float y, float zoom, u8 alpha) {
  if (s->page >= scene_tex_n) return;
  draw_tex_xy(&scene_tex[s->page], s->sx, s->sy, s->w, s->h, x, y, zoom * s->dw / s->w, zoom * s->dh / s->h, false,
              C2D_Color32(255, 255, 255, alpha));
}

void gfx_sprite_xy(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  if (page < 0 || page >= page_count) return;
  draw_tex_xy(&pages[page], sx, sy, sw, sh, x, y, kx, ky, flip, color);
}

void gfx_sprite_solid(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  if (page < 0 || page >= page_count) return;
  C2D_Flush();
  C2D_SetTintMode(C2D_TintSolid);
  draw_tex_xy(&pages[page], sx, sy, sw, sh, x, y, kx, ky, flip, color);
  C2D_Flush();
  C2D_SetTintMode(C2D_TintMult);
}

void gfx_sprite(int page, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color) {
  if (page < 0 || page >= page_count) return;
  draw_tex(&pages[page], sx, sy, sw, sh, x, y, scale, flip, color);
}

void gfx_furni(u16 def, const FurniPart *p, float x, float y, float scale, u32 color) {
  C3D_Tex *tex = furni_tex(def);
  if (tex) draw_tex(tex, p->x, p->y, p->w, p->h, x, y, scale, false, color);
}

void gfx_furni_icon(u16 def, float cx, float cy, float box) {
  const FurniDef *d = def_get(def);
  C3D_Tex *tex = d && d->icon_w ? furni_tex(def) : NULL;
  if (!tex) return;
  float k = 1;
  if (d->icon_w > box || d->icon_h > box) k = box / (d->icon_w > d->icon_h ? d->icon_w : d->icon_h);
  draw_tex(tex, d->icon_x, d->icon_y, d->icon_w, d->icon_h, cx - d->icon_w * k / 2, cy - d->icon_h * k / 2, k, false, 0xFFFFFFFF);
}

void gfx_filter(bool on) {
  if (on == linear) return;
  linear = on;
  C2D_Flush();
  GPU_TEXTURE_FILTER_PARAM f = on ? GPU_LINEAR : GPU_NEAREST;
  for (int i = 0; i < page_count; i++) C3D_TexSetFilter(&pages[i], f, f);
  for (int i = 0; i < MAX_ROOMS; i++) if (rooms[i].loaded) C3D_TexSetFilter(&rooms[i].tex, f, f);
  for (int i = 0; i < FURNI_SLOTS; i++) if (furni[i].loaded) C3D_TexSetFilter(&furni[i].tex, f, f);
  for (int i = 0; i < scene_tex_n; i++) C3D_TexSetFilter(&scene_tex[i], f, f);
}

void gfx_additive(bool on) {
  C2D_Flush();
  if (on) C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
  else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

void gfx_room_release(void) {
  C2D_Flush();
  for (int k = 0; k < MAX_ROOMS; k++) if (rooms[k].loaded) { C3D_TexDelete(&rooms[k].tex); rooms[k].loaded = false; }
}

const RoomBackground *gfx_room(const char *model) {
  for (int i = 0; i < room_background_count; i++) {
    const RoomBackground *bg = &room_backgrounds[i];
    if (strcmp(bg->model, model)) continue;
    for (int k = 0; k < MAX_ROOMS; k++) if (rooms[k].loaded && rooms[k].bg == bg) return bg;
    // Only the current room background is kept: each takes up to 2 MB of linear memory.
    C2D_Flush();
    for (int k = 0; k < MAX_ROOMS; k++) if (rooms[k].loaded) { C3D_TexDelete(&rooms[k].tex); rooms[k].loaded = false; }
    for (int k = 0; k < MAX_ROOMS; k++) {
      if (rooms[k].loaded) continue;
      char path[96];
      snprintf(path, sizeof path, "romfs:/gfx/%s.t3x", bg->file);
      if (!load_t3x(path, &rooms[k].tex)) return NULL;
      if (linear) C3D_TexSetFilter(&rooms[k].tex, GPU_LINEAR, GPU_LINEAR);
      rooms[k].bg = bg; rooms[k].loaded = true;
      return bg;
    }
  }
  return NULL;
}

void gfx_room_draw(const RoomBackground *bg, float x, float y, float scale) {
  for (int k = 0; k < MAX_ROOMS; k++)
    if (rooms[k].loaded && rooms[k].bg == bg) draw_tex(&rooms[k].tex, 0, 0, bg->w, bg->h, x, y, scale, false, 0xFFFFFFFF);
}

// Text cache: parsing a string (glyph lookup) costs more than drawing it, and almost every
// text repeats from one frame to the next. This matters most on Old 3DS and 2DS.
#define TC_SLOTS 512
#define TC_KEY 96
static struct { bool used; u32 hash; char key[TC_KEY]; C2D_Text text; float w; } tc[TC_SLOTS];
static int tc_count;

static u32 fnv(const char *s) {
  u32 h = 2166136261u;
  while (*s) { h ^= (u8)*s++; h *= 16777619u; }
  return h;
}

static void tc_clear(void) {
  C2D_TextBufClear(text_buf);
  memset(tc, 0, sizeof tc);
  tc_count = 0;
}

/** Parsed text (cached when short); NULL on failure. `w` = width at scale 1. */
static C2D_Text *text_get(const char *s, float *w) {
  static C2D_Text tmp;
  static C2D_TextBuf scratch;
  size_t len = strlen(s);
  if (len >= TC_KEY) {
    if (!scratch) scratch = C2D_TextBufNew(1024);
    C2D_TextBufClear(scratch);
    if (!C2D_TextParse(&tmp, scratch, s)) return NULL;
    C2D_TextOptimize(&tmp);
    C2D_TextGetDimensions(&tmp, 1, 1, w, NULL);
    return &tmp;
  }
  u32 h = fnv(s);
  for (int probe = 0; probe < TC_SLOTS; probe++) {
    int i = (int)((h + (u32)probe) % TC_SLOTS);
    if (!tc[i].used) {
      // Full (or glyph buffer exhausted): clear and start over.
      if (tc_count > TC_SLOTS * 3 / 4) { tc_clear(); return text_get(s, w); }
      if (!C2D_TextParse(&tc[i].text, text_buf, s)) {
        if (!tc_count) return NULL;
        tc_clear();
        return text_get(s, w);
      }
      C2D_TextOptimize(&tc[i].text);
      C2D_TextGetDimensions(&tc[i].text, 1, 1, &tc[i].w, NULL);
      tc[i].used = true;
      tc[i].hash = h;
      memcpy(tc[i].key, s, len + 1);
      tc_count++;
      *w = tc[i].w;
      return &tc[i].text;
    }
    if (tc[i].hash == h && !strcmp(tc[i].key, s)) { *w = tc[i].w; return &tc[i].text; }
  }
  return NULL;
}

void text_begin(void) {}

// The system font is about 30 px tall at scale 1.
#define FONT_PX 30.0f

/** Very small system font sizes read poorly on the bottom screen: 9 px minimum. */
static float legible(float size) { return size < 9 ? 9 : size < 10.5f ? size + 0.5f : size; }

float text_draw(const char *s, float x, float y, float size, u32 color, int align) {
  float w1;
  size = legible(size);
  C2D_Text *t = text_get(s, &w1);
  if (!t) return 0;
  float k = size / FONT_PX, w = w1 * k;
  if (align == 1) x -= w / 2; else if (align == 2) x -= w;
  // Whole-pixel position: scaled-down text looks sharper.
  C2D_DrawText(t, C2D_WithColor, roundf(x), roundf(y), 0.5f, k, k, color);
  return w;
}

float text_width(const char *s, float size) {
  float w1;
  return text_get(s, &w1) ? w1 * legible(size) / FONT_PX : 0;
}

void text_fit(char *s, float size, float max) {
  if (text_width(s, size) <= max) return;
  int n = (int)strlen(s);
  while (n > 0) {
    n--;
    while (n > 0 && ((u8)s[n] & 0xC0) == 0x80) n--;
    memcpy(s + n, "\xE2\x80\xA6", 4);
    if (text_width(s, size) <= max) return;
  }
}

void fill_rect(float x, float y, float w, float h, u32 color) { C2D_DrawRectSolid(x, y, 0.5f, w, h, color); }

void fill_round(float x, float y, float w, float h, float r, u32 color) {
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  C2D_DrawRectSolid(x + r, y, 0.5f, w - 2 * r, h, color);
  C2D_DrawRectSolid(x, y + r, 0.5f, r, h - 2 * r, color);
  C2D_DrawRectSolid(x + w - r, y + r, 0.5f, r, h - 2 * r, color);
  C2D_DrawCircleSolid(x + r, y + r, 0.5f, r, color);
  C2D_DrawCircleSolid(x + w - r, y + r, 0.5f, r, color);
  C2D_DrawCircleSolid(x + r, y + h - r, 0.5f, r, color);
  C2D_DrawCircleSolid(x + w - r, y + h - r, 0.5f, r, color);
}
