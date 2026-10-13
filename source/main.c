// Tweebbo for Nintendo 3DS: online client for tweebbo.lol (or another server) with
// an offline simulation as a fallback. Top screen: the room. Bottom screen: the interface.
#include <3ds.h>
#include <citro2d.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "conn.h"
#include "gfx.h"
#include "net.h"
#include "scene.h"
#include "sim.h"
#include "ui.h"
#include "update.h"
#include "world.h"

#define DATA_DIR "sdmc:/3ds/Tweebbo"
#define PROFILE DATA_DIR "/profile.txt"

// The default stack (32 KB) is too small for the nested simulation.
u32 __stacksize__ = 256 * 1024;

u64 now_ms(void) { return osGetTime(); }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + (int)((u32)rand() % (u32)(hi - lo)); }

void on_room(void) { u64 now = now_ms(); scene_room_changed(now); ui_room_changed(); }
void on_chat(u32 eid, const char *text) {
  if (ui_chat_blocked(eid)) return;
  scene_chat(eid, text, now_ms());
  ui_chat(eid, text);
}
void on_notice(const char *text) { ui_notice(text); ui_toast(text, now_ms()); }
void on_transfer(const char *slug, const char *arrival) { net_goto(slug, arrival); }
void on_alert(const char *json) { ui_alert(json); }
void on_social_changed(void) { ui_social_changed(); }
void on_name_changed(u32 eid) { ui_name_changed(eid); }
void on_item(u32 id) { ui_item_changed(id); }

static FILE *log_file;

void dbg_log(const char *fmt, ...) {
  if (!log_file) {
    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    log_file = fopen(DATA_DIR "/log.txt", "w");
    if (!log_file) return;
  }
  va_list ap;
  va_start(ap, fmt);
  fprintf(log_file, "[%8llu] ", (unsigned long long)osGetTime() % 100000000ull);
  vfprintf(log_file, fmt, ap);
  fputc('\n', log_file);
  va_end(ap);
  fflush(log_file);
}

// Settings and per-server token on the SD card (never shown on screen).
static void store_path(const char *key, char *path, int cap) {
  char safe[96];
  int n = 0;
  for (const char *k = key; *k && n < (int)sizeof safe - 1; k++)
    safe[n++] = (*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') || (*k >= '0' && *k <= '9') || *k == '-' || *k == '.' ? *k : '_';
  safe[n] = 0;
  snprintf(path, (size_t)cap, DATA_DIR "/%s.txt", safe);
}

bool store_load(const char *key, char *out, int cap) {
  char path[160];
  store_path(key, path, sizeof path);
  FILE *f = fopen(path, "r");
  if (!f) return false;
  int n = (int)fread(out, 1, (size_t)cap - 1, f);
  fclose(f);
  out[n > 0 ? n : 0] = 0;
  out[strcspn(out, "\r\n")] = 0;
  return true;
}

void store_save(const char *key, const char *value) {
  mkdir("sdmc:/3ds", 0777);
  mkdir(DATA_DIR, 0777);
  char path[160];
  store_path(key, path, sizeof path);
  FILE *f = fopen(path, "w");
  if (!f) return;
  fputs(value, f);
  fclose(f);
}

// The offline profile replaces the server database.
void profile_load(char *name, int cap, u64 *look, u32 *credits) {
  name[0] = 0; *look = 0; *credits = 200;
  FILE *f = fopen(PROFILE, "r");
  if (!f) return;
  char line[128];
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "name=", 5)) snprintf(name, (size_t)cap, "%s", line + 5);
    else if (!strncmp(line, "look=", 5)) *look = strtoull(line + 5, NULL, 10);
    else if (!strncmp(line, "credits=", 8)) *credits = (u32)strtoul(line + 8, NULL, 10);
  }
  fclose(f);
}

void profile_save(const char *name, u64 look, u32 credits) {
  mkdir("sdmc:/3ds", 0777);
  mkdir(DATA_DIR, 0777);
  FILE *f = fopen(PROFILE, "w");
  if (!f) return;
  fprintf(f, "name=%s\nlook=%llu\ncredits=%u\n", name, (unsigned long long)look, (unsigned)credits);
  fclose(f);
}

static void fatal(const char *msg) {
  consoleInit(GFX_BOTTOM, NULL);
  printf("\x1b[2J\x1b[1;1HTweebbo\n\n%s\n\nPress START to exit.", msg);
  while (aptMainLoop()) {
    hidScanInput();
    if (hidKeysDown() & KEY_START) break;
    gspWaitForVBlank();
  }
}

int main(int argc, char **argv) {
  update_set_launch_path(argc > 0 ? argv[0] : NULL);
  gfxInitDefault();
  romfsInit();
  srand((unsigned)osGetTime());
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(8192);
  C2D_Prepare();
  C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

  bool n3ds = false;
  APT_CheckNew3DS(&n3ds);
  dbg_log("Tweebbo %s on %s; app memory free %lu KB, linear free %lu KB", APP_VERSION, n3ds ? "New 3DS" : "Old 3DS/2DS",
          (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024), (unsigned long)(linearSpaceFree() / 1024));
  if (!gfx_init()) {
    C2D_Fini(); C3D_Fini();
    fatal("Could not load the graphics from romfs.");
    romfsExit(); gfxExit();
    return 1;
  }
  // On New 3DS, 804 MHz gives the avatar compositor plenty of headroom.
  osSetSpeedupEnable(true);
  dbg_log("after gfx_init: linear free %lu KB", (unsigned long)(linearSpaceFree() / 1024));
  net_start(now_ms());

  while (aptMainLoop()) {
    hidScanInput();
    u64 now = now_ms();
    if (!ui_input(hidKeysDown(), hidKeysHeld(), now)) break;
    // Once inside the hotel, check for a newer version (once per session).
    static bool checked;
    if (!checked && W.in_room && net_online()) { checked = true; update_check(); }
    net_update(now);
    now = now_ms();

    gfx_frame();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    text_begin();
    C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(top);
    scene_draw(now);
    C2D_TargetClear(bottom, C2D_Color32(0x1b, 0x25, 0x2c, 255));
    C2D_SceneBegin(bottom);
    ui_draw(now);
    C3D_FrameEnd(0);
    // Real performance on the console: average CPU/GPU time per frame every 30 s in log.txt.
    static float cpu_sum, gpu_sum, worst;
    static int frames;
    static u64 since;
    float cpu = C3D_GetProcessingTime(), gpu = C3D_GetDrawingTime();
    cpu_sum += cpu; gpu_sum += gpu; frames++;
    if (cpu > worst) worst = cpu;
    if (!since) since = now;
    if (now - since > 30000) {
      dbg_log("frame: cpu %.1f ms avg (worst %.1f), gpu %.1f ms, %d avatars, linear free %lu KB", cpu_sum / frames, worst,
              gpu_sum / frames, scene_avatar_count(), (unsigned long)(linearSpaceFree() / 1024));
      cpu_sum = gpu_sum = worst = 0; frames = 0; since = now;
    }
  }

  net_stop();
  conn_global_exit();
  gfx_exit();
  C2D_Fini();
  C3D_Fini();
  romfsExit();
  gfxExit();
  return 0;
}
