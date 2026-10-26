#include "gfx.h"
#include "language.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "catalog.h"
#include "world.h"
#include <stdlib.h>
#include "gen/snow_assets.h"
#include "gen/fastfood_assets.h"
#include "gen/battle_assets.h"

#define MAX_PAGES 24
#define MAX_ROOMS 8

static C3D_Tex pages[MAX_PAGES];
static int page_count;
// Pages from atlas_page_resident on (character skins) load when a sprite on them is drawn and
// go away after PAGE_IDLE_FRAMES without use: they are large and most rooms have no skins.
#define PAGE_IDLE_FRAMES 600
static bool page_loaded[MAX_PAGES];
static u32 page_used[MAX_PAGES], page_retry[MAX_PAGES];
static struct { const RoomBackground *bg; C3D_Tex tex[4]; bool loaded; char wall[9], floor[9]; } rooms[MAX_ROOMS];
static C2D_TextBuf text_buf;
static C3D_Tex font_texture;
static C3D_Tex cjk_texture;
static bool cjk_loaded;
static bool linear;
static void scene_free(void);
static bool furni_release_all(void);
static void draw_tex(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color);
static void draw_tex_xy(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color);
static C3D_Tex snow_pages[12];
static C3D_Tex fastfood_pages[2];
static unsigned fastfood_loaded;
static unsigned snow_loaded;

// Furniture textures: one per definition, within a linear-memory budget.
#define FURNI_SLOTS 160
#define FURNI_BUDGET_MAX (16u * 1024 * 1024)
/** Adjusted in gfx_init to the free linear memory (Old 3DS has much less). */
static u32 furni_budget = 8u * 1024 * 1024;
static struct { u16 def; u8 page; bool loaded, failed; C3D_Tex tex; u32 used, retry; } furni[FURNI_SLOTS];
static u32 frame_no, furni_bytes;
// Anti-thrash: a texture drawn in the last FURNI_KEEP_FRAMES frames is never evicted to make
// room for another one (the newcomer waits and retries instead), and at most
// FURNI_LOADS_PER_FRAME textures are read from romfs per frame. Without this, a room whose
// furniture does not fit in memory evicts and reloads textures every frame.
#define FURNI_KEEP_FRAMES 30
#define FURNI_LOADS_PER_FRAME 4
static int frame_loads;
/** Texture traffic since the last gfx_perf_take(): loads from romfs and evictions. */
static u32 perf_loads, perf_evictions;

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
  page_count = atlas_page_count < MAX_PAGES ? atlas_page_count : MAX_PAGES;
  for (int i = 0; i < atlas_page_resident && i < page_count; i++) {
    snprintf(path, sizeof path, "romfs:/gfx/%s.t3x", atlas_page_files[i]);
    if (!load_t3x(path, &pages[i])) return false;
    page_loaded[i] = true;
  }
  if (!load_t3x("romfs:/gfx/font0.t3x", &font_texture)) return false;
  text_buf = C2D_TextBufNew(16384);
  // The system font is drawn scaled down (≈1/3): linear filtering keeps it smooth instead of jagged.
  C2D_FontSetFilter(NULL, GPU_LINEAR, GPU_LINEAR);
  // Keep 8 MB for room backgrounds, on-demand skin pages, text, music and citro2d buffers.
  u32 free_linear = linearSpaceFree();
  furni_budget = free_linear > 12u * 1024 * 1024 ? free_linear - 8u * 1024 * 1024 : 4u * 1024 * 1024;
  if (furni_budget > FURNI_BUDGET_MAX) furni_budget = FURNI_BUDGET_MAX;
  dbg_log("atlas pages %d, furniture budget %lu KB", page_count, (unsigned long)(furni_budget / 1024));
  // Avatar parts and colored furniture are tinted by multiplication,
  // like spriteImage() in the web client. Shapes and text are not affected.
  C2D_SetTintMode(C2D_TintMult);
  return true;
}

bool gfx_language_font(int language) {
  C3D_FrameSync();
  if(language==TW_ZH||language==TW_JA){
    C3D_Tex next;
    if(!load_t3x(language==TW_JA?"romfs:/gfx/font-ja.t3x":"romfs:/gfx/font-zh.t3x",&next))return false;
    if(cjk_loaded)C3D_TexDelete(&cjk_texture);
    cjk_texture=next;cjk_loaded=true;
  }else{if(cjk_loaded)C3D_TexDelete(&cjk_texture);cjk_loaded=false;}
  return true;
}
void gfx_cjk_sprite(int sx,int sy,int sw,int sh,float x,float y,unsigned color){if(cjk_loaded)draw_tex(&cjk_texture,sx,sy,sw,sh,x,y,1,false,color);}

static int furni_victim(int exclude) {
  int victim = -1;
  for (int i = 0; i < FURNI_SLOTS; i++)
    if (i != exclude && furni[i].loaded && furni[i].used + FURNI_KEEP_FRAMES < frame_no &&
        (victim < 0 || furni[i].used < furni[victim].used)) victim = i;
  return victim;
}

static void furni_evict(int i) {
  perf_evictions++;
  C2D_Flush();
  furni_bytes -= furni[i].tex.size;
  C3D_TexDelete(&furni[i].tex);
  furni[i].loaded = furni[i].failed = false;
}

static C3D_Tex *furni_tex(u16 def, u8 page) {
  u32 bytes = 0;
  int lo = 0, hi = furni_texture_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const FurniTexture *t = &furni_textures[mid];
    if (t->def == def && t->page == page) { bytes = t->bytes; break; }
    if (t->def < def || (t->def == def && t->page < page)) lo = mid + 1; else hi = mid - 1;
  }
  if (!bytes) return NULL;
  int slot = -1;
  for (int i = 0; i < FURNI_SLOTS; i++) {
    if ((furni[i].loaded || furni[i].failed) && furni[i].def == def && furni[i].page == page) { slot = i; break; }
    if (!furni[i].loaded && (!furni[i].failed || frame_no >= furni[i].retry) && slot < 0) slot = i;
  }
  if (slot < 0) {
    slot = furni_victim(-1);
    if (slot < 0) return NULL;
    furni_evict(slot);
  }
  if (furni[slot].loaded) { furni[slot].used = frame_no; return &furni[slot].tex; }
  if (furni[slot].failed && frame_no < furni[slot].retry) return NULL;
  if (frame_loads >= FURNI_LOADS_PER_FRAME) return NULL;  // drawn from the next frame on
  // Evict before allocating, preserving textures already queued in this frame.
  while (furni_bytes + bytes > furni_budget || linearSpaceFree() < bytes + 1024u * 1024) {
    int victim = furni_victim(slot);
    if (victim < 0) break;
    furni_evict(victim);
  }
  furni[slot].def = def;
  furni[slot].page = page;
  furni[slot].used = frame_no;
  furni[slot].failed = false;
  if (furni_bytes + bytes > furni_budget || linearSpaceFree() < bytes + 1024u * 1024) {
    furni[slot].failed = true; furni[slot].retry = frame_no + 60;
    return NULL;
  }
  char path[64];
  snprintf(path, sizeof path, "romfs:/furni/d%u_%u.t3x", (unsigned)def, (unsigned)page);
  perf_loads++;
  frame_loads++;
  if (!load_t3x(path, &furni[slot].tex)) {
    furni[slot].failed = true; furni[slot].retry = frame_no + 300;
    return NULL;
  }
  if (linear) C3D_TexSetFilter(&furni[slot].tex, GPU_LINEAR, GPU_LINEAR);
  furni[slot].loaded = true;
  furni_bytes += furni[slot].tex.size;
  return &furni[slot].tex;
}

void gfx_furni_preload(u16 def) { furni_tex(def, 0); }

void gfx_perf_take(u32 *loads, u32 *evictions, u32 *bytes, u32 *budget) {
  *loads = perf_loads; *evictions = perf_evictions; *bytes = furni_bytes; *budget = furni_budget;
  perf_loads = perf_evictions = 0;
}

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

/** An atlas page, loading an on-demand (skin) page the first time it is drawn. */
static C3D_Tex *page_tex(int page) {
  if (page < 0 || page >= page_count) return NULL;
  if (!page_loaded[page]) {
    if (frame_no < page_retry[page]) return NULL;
    char path[64];
    snprintf(path, sizeof path, "romfs:/gfx/%s.t3x", atlas_page_files[page]);
    bool ok = load_t3x(path, &pages[page]);
    if (!ok && furni_release_all()) ok = load_t3x(path, &pages[page]);
    if (!ok) { page_retry[page] = frame_no + 120; return NULL; }
    if (linear) C3D_TexSetFilter(&pages[page], GPU_LINEAR, GPU_LINEAR);
    page_loaded[page] = true;
    perf_loads++;
  }
  page_used[page] = frame_no;
  return &pages[page];
}

void gfx_frame(void) {
  frame_no++;
  frame_loads = 0;
  for (int i = atlas_page_resident; i < page_count; i++)
    if (page_loaded[i] && page_used[i] + PAGE_IDLE_FRAMES < frame_no) {
      C2D_Flush();
      C3D_TexDelete(&pages[i]);
      page_loaded[i] = false;
    }
  while (furni_bytes > furni_budget) {
    int victim = -1;
    for (int k = 0; k < FURNI_SLOTS; k++)
      if (furni[k].loaded && furni[k].used + 1 < frame_no && (victim < 0 || furni[k].used < furni[victim].used)) victim = k;
    if (victim < 0) break;
    furni_evict(victim);
  }
}

void gfx_exit(void) {
  gfx_snow_free();
  gfx_fastfood_free();
  gfx_battle_free();
  scene_free();
  for (int i = 0; i < PET_SLOTS; i++) if (pet_tex[i].loaded) C3D_TexDelete(&pet_tex[i].tex);
  for (int i = 0; i < FURNI_SLOTS; i++) if (furni[i].loaded) C3D_TexDelete(&furni[i].tex);
  for (int i = 0; i < page_count; i++) if (page_loaded[i]) C3D_TexDelete(&pages[i]);
  for (int i = 0; i < MAX_ROOMS; i++) if (rooms[i].loaded) for (int j = 0; j < rooms[i].bg->tile_count; j++) C3D_TexDelete(&rooms[i].tex[j]);
  C3D_TexDelete(&font_texture);
  if(cjk_loaded)C3D_TexDelete(&cjk_texture);
  C2D_TextBufDelete(text_buf);
}

void gfx_suspend_room(void) {
  scene_free();
  gfx_room_release();
  for (int i=0;i<FURNI_SLOTS;i++) if(furni[i].loaded) furni_evict(i);
  for (int i=0;i<PET_SLOTS;i++) if(pet_tex[i].loaded) { C3D_TexDelete(&pet_tex[i].tex); pet_tex[i].loaded=false; }
}
bool gfx_snow_load(void) {
  if(snow_loaded==snow_page_count)return true;
  gfx_snow_free();
  if(snow_page_count>COUNT(snow_pages))return false;
  for(unsigned i=0;i<snow_page_count;i++) {
    char path[96];snprintf(path,sizeof path,"romfs:/snow/%s.t3x",snow_page_files[i]);
    if(!load_t3x(path,&snow_pages[i])){gfx_snow_free();return false;}
    snow_loaded++;
  }
  return true;
}
void gfx_snow_free(void) {
  for(unsigned i=0;i<snow_loaded;i++)C3D_TexDelete(&snow_pages[i]);
  snow_loaded=0;
}
void gfx_snow_sprite(unsigned id,float x,float y,float kx,float ky,u32 color,float left,float top,float right,float bottom) {
  if(id>=snow_sprite_count||!kx||ky<=0)return;
  const SnowSprite *s=&snow_sprites[id];
  if(s->page>=snow_loaded)return;
  bool flip=kx<0;float scale=fabsf(kx);
  x+=flip?-(s->ox+s->w)*scale:s->ox*scale;y+=s->oy*ky;
  int l=(int)fmaxf(0,ceilf((left-x)/scale)),t=(int)fmaxf(0,ceilf((top-y)/ky));
  int r=(int)fminf(s->w,floorf((right-x)/scale)),b=(int)fminf(s->h,floorf((bottom-y)/ky));
  if(r<=l||b<=t)return;
  int sx=s->x+(flip?s->w-r:l);
  draw_tex_xy(&snow_pages[s->page],sx,s->y+t,r-l,b-t,x+l*scale,y+t*ky,scale,ky,flip,color);
}

bool gfx_fastfood_load(void) {
  if(fastfood_loaded==fastfood_page_count)return true;
  gfx_fastfood_free();
  if(fastfood_page_count>COUNT(fastfood_pages))return false;
  for(unsigned i=0;i<fastfood_page_count;i++) {
    char path[96];snprintf(path,sizeof path,"romfs:/fastfood/%s.t3x",fastfood_page_files[i]);
    if(!load_t3x(path,&fastfood_pages[i])){gfx_fastfood_free();return false;}
    C3D_TexSetFilter(&fastfood_pages[i], GPU_LINEAR, GPU_LINEAR);
    fastfood_loaded++;
  }
  return true;
}
void gfx_fastfood_free(void) {
  for(unsigned i=0;i<fastfood_loaded;i++)C3D_TexDelete(&fastfood_pages[i]);
  fastfood_loaded=0;
}
void gfx_fastfood_sprite(unsigned id,float x,float y,float kx,float ky,u32 color,float left,float top,float right,float bottom) {
  if(id>=fastfood_sprite_count||!kx||ky<=0)return;
  const FastfoodSprite *s=&fastfood_sprites[id];
  if(s->page>=fastfood_loaded)return;
  bool flip=kx<0;float scale=fabsf(kx);
  x+=flip?-(s->ox+s->w)*scale:s->ox*scale;y+=s->oy*ky;
  int l=(int)fmaxf(0,ceilf((left-x)/scale)),t=(int)fmaxf(0,ceilf((top-y)/ky));
  int r=(int)fminf(s->w,floorf((right-x)/scale)),b=(int)fminf(s->h,floorf((bottom-y)/ky));
  if(r<=l||b<=t)return;
  int sx=s->x+(flip?s->w-r:l);
  draw_tex_xy(&fastfood_pages[s->page],sx,s->y+t,r-l,b-t,x+l*scale,y+t*ky,scale,ky,flip,color);
}

#define BATTLE_TEXTURE C3D_Tex
#define BATTLE_FREE(tex) C3D_TexDelete(&(tex))
#define BATTLE_LOAD(path,target) load_t3x(path,target)
#define BATTLE_DRAW(tex,...) draw_tex_xy(&(tex),__VA_ARGS__)
#define BATTLE_DIRECTORY "romfs:/battle"
#define BATTLE_EXTENSION ".t3x"
#include "battle_textures.inc"

void gfx_battle_blend(int mode){
  C2D_Flush();
  C3D_AlphaBlend(mode==2?GPU_BLEND_REVERSE_SUBTRACT:GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,mode?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
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
  // The scene pages are the room (up to 2x 1024x1024): they take priority over the previous
  // room background and the cached furniture, both of which load again on demand. Without
  // this, a two-page Lido scene does not fit in the linear heap and the room stays black.
  gfx_room_release();
  for (int i = 0; i < want->pages && i < SCENE_PAGES; i++) {
    char path[64];
    snprintf(path, sizeof path, "romfs:/gfx/scene_%s_%d.t3x", want->model, i);
    if (!load_t3x(path, &scene_tex[i]) && !(furni_release_all() && load_t3x(path, &scene_tex[i]))) { scene_free(); return NULL; }
    if (linear) C3D_TexSetFilter(&scene_tex[i], GPU_LINEAR, GPU_LINEAR);
    scene_tex_n = i + 1;
  }
  scene_loaded = want;
  return want;
}

void gfx_scene_sprite(const SceneSprite *s, float x, float y, float zoom, u8 alpha) {
  if (s->page >= scene_tex_n) return;
  if (s->additive) gfx_additive(true);
  draw_tex_xy(&scene_tex[s->page], s->sx, s->sy, s->w, s->h, x, y, zoom * s->dw / s->w, zoom * s->dh / s->h, false,
              C2D_Color32(255, 255, 255, alpha));
  if (s->additive) gfx_additive(false);
}

const SceneArt *gfx_art(const char *name) {
  if (!scene_loaded) return NULL;
  int lo = 0, hi = scene_loaded->art_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(scene_loaded->art[mid].name, name);
    if (!c) return &scene_loaded->art[mid];
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

void gfx_art_draw(const SceneArt *a, float x, float y, float kx, float ky, u32 color) {
  if (!a || a->page >= scene_tex_n) return;
  draw_tex_xy(&scene_tex[a->page], a->sx, a->sy, a->w, a->h, x, y, kx, ky, false, color);
}

void gfx_sprite_xy(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  C3D_Tex *tex = page_tex(page);
  if (tex) draw_tex_xy(tex, sx, sy, sw, sh, x, y, kx, ky, flip, color);
}

void gfx_sprite_solid(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  C3D_Tex *tex = page_tex(page);
  if (!tex) return;
  C2D_Flush();
  C2D_SetTintMode(C2D_TintSolid);
  draw_tex_xy(tex, sx, sy, sw, sh, x, y, kx, ky, flip, color);
  C2D_Flush();
  C2D_SetTintMode(C2D_TintMult);
}

void gfx_sprite(int page, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color) {
  C3D_Tex *tex = page_tex(page);
  if (tex) draw_tex(tex, sx, sy, sw, sh, x, y, scale, flip, color);
}

void gfx_furni(u16 def, const FurniPart *p, float x, float y, float scale, u32 color) {
  C3D_Tex *tex = furni_tex(def, p->page);
  if (tex) draw_tex(tex, p->x, p->y, p->w, p->h, x, y, scale, false, color);
}

void gfx_furni_icon(u16 def, float cx, float cy, float box) {
  const FurniDef *d = def_get(def);
  C3D_Tex *tex = d && d->icon_w ? furni_tex(def, d->icon_page) : NULL;
  if (!tex) return;
  float k = 1;
  if (d->icon_w > box || d->icon_h > box) k = box / (d->icon_w > d->icon_h ? d->icon_w : d->icon_h);
  draw_tex(tex, d->icon_x, d->icon_y, d->icon_w, d->icon_h, cx - d->icon_w * k / 2, cy - d->icon_h * k / 2, k, false, 0xFFFFFFFF);
}

void gfx_furni_icon_scaled(u16 def, float cx, float cy, float k) {
  const FurniDef *d = def_get(def);
  C3D_Tex *tex = d && d->icon_w ? furni_tex(def, d->icon_page) : NULL;
  if (!tex) return;
  draw_tex(tex, d->icon_x, d->icon_y, d->icon_w, d->icon_h, roundf(cx - d->icon_w * k / 2), roundf(cy - d->icon_h * k / 2), k, false, 0xFFFFFFFF);
}

void gfx_filter(bool on) {
  if (on == linear) return;
  linear = on;
  C2D_Flush();
  GPU_TEXTURE_FILTER_PARAM f = on ? GPU_LINEAR : GPU_NEAREST;
  for (int i = 0; i < page_count; i++) if (page_loaded[i]) C3D_TexSetFilter(&pages[i], f, f);
  for (int i = 0; i < MAX_ROOMS; i++) if (rooms[i].loaded) for (int j = 0; j < rooms[i].bg->tile_count; j++) C3D_TexSetFilter(&rooms[i].tex[j], f, f);
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
  for (int k = 0; k < MAX_ROOMS; k++) if (rooms[k].loaded) { for (int j = 0; j < rooms[k].bg->tile_count; j++) C3D_TexDelete(&rooms[k].tex[j]); rooms[k].loaded = false; }
}

static bool furni_release_all(void) {
  bool freed = false;
  for (int i = 0; i < FURNI_SLOTS; i++) if (furni[i].loaded) { furni_evict(i); freed = true; }
  if (freed) dbg_log("released furniture cache for a room page (linear free %lu KB)", (unsigned long)(linearSpaceFree() / 1024));
  return freed;
}

const RoomBackground *gfx_room(const char *model) {
  for (int i = 0; i < room_background_count; i++) {
    const RoomBackground *bg = &room_backgrounds[i];
    if (strcmp(bg->model, model)) continue;
    for (int k = 0; k < MAX_ROOMS; k++) if (rooms[k].loaded && rooms[k].bg == bg) return bg;
    // Only the current room background is kept: each takes up to 2 MB of linear memory.
    C2D_Flush();
    for (int k = 0; k < MAX_ROOMS; k++) if (rooms[k].loaded) { for (int j = 0; j < rooms[k].bg->tile_count; j++) C3D_TexDelete(&rooms[k].tex[j]); rooms[k].loaded = false; }
    for (int k = 0; k < MAX_ROOMS; k++) {
      if (rooms[k].loaded) continue;
      rooms[k].bg = bg;
      if (bg->tile_count > 4) return NULL;
      for (int j = 0; j < bg->tile_count; j++) {
        char path[96];
        snprintf(path, sizeof path, "romfs:/gfx/%s.t3x", bg->tiles[j].file);
        // Room pages are large single allocations: free cached furniture (redrawn on demand) and retry once.
        if (!load_t3x(path, &rooms[k].tex[j]) && !(furni_release_all() && load_t3x(path, &rooms[k].tex[j]))) {
          for (int t = 0; t < j; t++) C3D_TexDelete(&rooms[k].tex[t]);
          return NULL;
        }
        if (linear) C3D_TexSetFilter(&rooms[k].tex[j], GPU_LINEAR, GPU_LINEAR);
      }
      rooms[k].loaded = true;
      rooms[k].wall[0] = rooms[k].floor[0] = 0;
      return bg;
    }
  }
  return NULL;
}

static const FinishChoice *finish_choice(int kind, const char *id) {
  for (int i = 0; i < finish_choice_count; i++) if (finish_choices[i].kind == kind && !strcmp(finish_choices[i].id, id)) return &finish_choices[i];
  return NULL;
}

static u8 *finish_pixels(const FinishChoice *f) {
  if (!f) return NULL;
  char path[128]; snprintf(path, sizeof path, "romfs:/spaces/%s.rgb", f->texture);
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  size_t bytes = (size_t)f->w * f->h * 3;
  u8 *data = malloc(bytes);
  if (data && fread(data, 1, bytes, file) != bytes) { free(data); data = NULL; }
  fclose(file);
  return data;
}

static unsigned morton(unsigned x, unsigned y) {
  return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

static void paint_finish(int slot) {
  const RoomBackground *bg = rooms[slot].bg;
  const FinishChoice *wall = finish_choice(1, W.wall), *floor = finish_choice(2, W.floor);
  u8 *wall_px = finish_pixels(wall), *floor_px = finish_pixels(floor);
  // Reset from the original texture when removing or changing either finish.
  for (int j = 0; j < bg->tile_count; j++) {
    char path[96]; snprintf(path, sizeof path, "romfs:/gfx/%s.t3x", bg->tiles[j].file);
    C3D_Tex replacement;
    if (!load_t3x(path, &replacement) && !(furni_release_all() && load_t3x(path, &replacement))) { free(wall_px); free(floor_px); return; }
    C3D_TexDelete(&rooms[slot].tex[j]);
    rooms[slot].tex[j] = replacement;
  }
  char path[96]; snprintf(path, sizeof path, "romfs:/spaces/%s.mask", bg->model);
  FILE *mask = fopen(path, "rb");
  u8 head[4];
  if (mask && fread(head, 1, 4, mask) == 4) {
    unsigned mw = head[0] | head[1] << 8, mh = head[2] | head[3] << 8, pos = 0;
    u8 run[4];
    while (mw && fread(run, 1, 4, mask) == 4) {
      unsigned count = run[0] | run[1] << 8;
      if (!count || pos + count > mw * mh) break;
      for (unsigned i = 0; i < count; i++, pos++) {
        unsigned x = pos % mw, y = pos / mw, label = run[2];
        bool is_wall = label == 3 || label == 4;
        const FinishChoice *choice = is_wall ? wall : label == 1 || label == 2 || label == 5 ? floor : NULL;
        u8 *texture = is_wall ? wall_px : floor_px;
        if (!choice || !texture) continue;
        int u = is_wall ? (int)x + bg->x : (int)floorf(((int)x + bg->x) / 2.f + (int)y + bg->y);
        int v = is_wall ? run[3] : (int)floorf((int)y + bg->y - ((int)x + bg->x) / 2.f);
        u = (u % choice->w + choice->w) % choice->w; v = (v % choice->h + choice->h) % choice->h;
        for (int j = 0; j < bg->tile_count; j++) {
          const RoomTile *tile = &bg->tiles[j];
          if (x < tile->x || y < tile->y || x >= tile->x + tile->w || y >= tile->y + tile->h) continue;
          unsigned tx = x - tile->x, ty = y - tile->y;
          C3D_Tex *tex = &rooms[slot].tex[j];
          unsigned texel = (((ty / 8) * (tex->width / 8) + tx / 8) * 64) + morton(tx & 7, ty & 7);
          u8 rgb[3];
          for (int channel = 0; channel < 3; channel++) {
            int color = choice->color >> (16 - channel * 8) & 255;
            if (label == 3) color = color > 16 ? color - 16 : 0;
            float factor = label == 2 ? .67f : label == 5 ? 1.f : texture[(v * choice->w + u) * 3 + channel] / 255.f;
            rgb[channel] = (u8)lrintf(color * factor);
          }
          if (tex->fmt == GPU_RGBA5551) {
            // Room backgrounds without soft alpha are RGBA5551 (half the memory): keep the alpha bit.
            u16 *pixel = (u16 *)tex->data + texel;
            *pixel = (u16)((rgb[0] >> 3) << 11 | (rgb[1] >> 3) << 6 | (rgb[2] >> 3) << 1 | (*pixel & 1));
          } else {
            u8 *pixel = (u8 *)tex->data + texel * 4;
            for (int channel = 0; channel < 3; channel++) pixel[3 - channel] = rgb[channel];
          }
        }
      }
    }
  }
  if (mask) fclose(mask);
  free(wall_px); free(floor_px);
  for (int j = 0; j < bg->tile_count; j++) {
    C3D_Tex *tex = &rooms[slot].tex[j];
    GSPGPU_FlushDataCache(tex->data, tex->size);
    C3D_TexSetFilter(tex, linear ? GPU_LINEAR : GPU_NEAREST, linear ? GPU_LINEAR : GPU_NEAREST);
  }
  snprintf(rooms[slot].wall, sizeof rooms[slot].wall, "%s", W.wall);
  snprintf(rooms[slot].floor, sizeof rooms[slot].floor, "%s", W.floor);
}

void gfx_room_draw(const RoomBackground *bg, float x, float y, float scale) {
  for (int k = 0; k < MAX_ROOMS; k++) if (rooms[k].loaded && rooms[k].bg == bg) {
    if (strcmp(rooms[k].wall, W.wall) || strcmp(rooms[k].floor, W.floor)) { C2D_Flush(); paint_finish(k); }
    for (int j = 0; j < bg->tile_count; j++) {
      const RoomTile *tile = &bg->tiles[j];
      draw_tex(&rooms[k].tex[j], 0, 0, tile->w, tile->h, x + tile->x * scale, y + tile->y * scale, scale, false, 0xFFFFFFFF);
    }
  }
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
static float legible(float size) { return size < 11 ? 11 : size; }

static unsigned utf8_next(const char **text) {
  const u8 *p = (const u8 *)*text;
  unsigned c = *p++, n = c < 128 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
  if (n) c &= (1u << (6 - n)) - 1;
  while (n-- && *p && (*p & 0xc0) == 0x80) c = c << 6 | (*p++ & 63);
  *text = (const char *)p;
  return c;
}

static const FontGlyph *glyph(unsigned code, int size) {
  if (code > 0xffff) return NULL;
  int lo = 0, hi = font_glyph_count - 1;
  unsigned key = (unsigned)size << 16 | code;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const FontGlyph *g = &font_glyphs[mid];
    unsigned k = (unsigned)g->size << 16 | g->code;
    if (key == k) return g;
    if (k < key) lo = mid + 1; else hi = mid - 1;
  }
  return cjk_loaded?tw_cjk_glyph(code):NULL;
}

static float bitmap_width(const char *s, int size) {
  float width = 0;
  while (*s) {
    const FontGlyph *g = glyph(utf8_next(&s), size);
    if (!g) return -1;
    width += g->advance;
  }
  return width;
}

static int bitmap_size(float size) {
  int px = (int)lroundf(legible(size));
  return px > 16 ? 16 : px;
}

float text_draw(const char *s, float x, float y, float size, u32 color, int align) {
  int px = bitmap_size(size);
  float width = bitmap_width(s, px);
  if (width >= 0) {
    if (align == 1) x -= width / 2; else if (align == 2) x -= width;
    x = roundf(x); y = roundf(y);
    while (*s) {
      const FontGlyph *g = glyph(utf8_next(&s), px);
      bool cjk=cjk_loaded&&g->code>255&&g==tw_cjk_glyph(g->code);
      draw_tex(cjk?&cjk_texture:&font_texture, g->x, g->y, g->w, g->h, x + g->left, y + px + g->top, 1, false, color);
      x += g->advance;
    }
    return width;
  }
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
  float width = bitmap_width(s, bitmap_size(size));
  if (width >= 0) return width;
  float w1;
  return text_get(s, &w1) ? w1 * legible(size) / FONT_PX : 0;
}

void text_fit(char *s, float size, float max) {
  if (text_width(s, size) <= max) return;
  int original = (int)strlen(s), n = original;
  if (original < 3 || text_width("…", size) > max) { s[0] = 0; return; }
  while (n > 0) {
    n--;
    while (n > 0 && ((u8)s[n] & 0xC0) == 0x80) n--;
    if (n + 3 > original) continue;
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
