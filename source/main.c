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
#include "showcase.h"
#include "sim.h"
#include "ui.h"
#include "snow.h"
#include "update.h"
#include "world.h"
#include "pool.h"
#include "bar.h"
#include "music.h"

#if defined(TWEEBBO_SHOWCASE)
#define DATA_DIR "sdmc:/3ds/Tweebbo-showcase"
#elif defined(TWEEBBO_DEBUG)
#define DATA_DIR "sdmc:/3ds/Tweebbo-debug"
#else
#define DATA_DIR "sdmc:/3ds/Tweebbo"
#endif
#define PROFILE DATA_DIR "/profile.txt"

// The default stack (32 KB) is too small for the nested simulation.
u32 __stacksize__ = 256 * 1024;

u64 now_ms(void) { return osGetTime(); }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + (int)((u32)rand() % (u32)(hi - lo)); }

void on_room(void) { u64 now = now_ms(); pool_room_changed(); music_room_changed(); scene_room_changed(now); ui_room_changed(); }
void on_chat(u32 eid, const char *text) {
  if (ui_chat_blocked(eid)) return;
  scene_chat(eid, text, now_ms());
  ui_chat(eid, text);
}
void on_notice(const char *text) { casino_notice(text); ui_notice(text); ui_toast(text, now_ms()); }
// Asked to leave a room: back to the lobby with the reason.
void on_kicked(const char *text) { ui_close(); ui_toast(text, now_ms()); net_goto("lobby", ""); }
void on_transfer(const char *slug, const char *arrival) { net_goto(slug, arrival); }
void on_alert(const char *json) { ui_alert(json); }
void on_casino(const char *json) { casino_receive(json, now_ms()); }
void on_trade(const char *json) { trade_receive(json, now_ms()); }
void on_activity(const char *json) { if (!bar_receive(json, now_ms())) pool_receive(json, now_ms()); }
void on_music(const char *json) { music_receive(json, now_ms()); }
void on_snow(const u8 *data, int len) { snow_receive(data,len); }
void on_social_changed(void) { ui_social_changed(); }
void on_name_changed(u32 eid) { ui_name_changed(eid); }
void on_item(u32 id) { ui_item_changed(id); }

static FILE *log_file;

void dbg_log(const char *fmt, ...) {
  if (!log_file) {
    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    // Keep the previous session's log: after a crash it is the one worth reading.
    remove(DATA_DIR "/log-previous.txt");
    rename(DATA_DIR "/log.txt", DATA_DIR "/log-previous.txt");
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

// Crash report: a CPU exception writes crash.txt (registers and the last log lines are enough to
// find the function with the matching .elf), then returns to the HOME Menu instead of freezing.
static u8 exception_stack[0x2000] __attribute__((aligned(8)));
static void on_exception(ERRF_ExceptionInfo *info, CpuRegisters *regs) {
  FILE *f = fopen(DATA_DIR "/crash.txt", "w");
  if (f) {
    fprintf(f, "Tweebbo %s crash\ntype %d fsr %08lx far %08lx\npc %08lx lr %08lx sp %08lx cpsr %08lx\n", APP_VERSION,
            (int)info->type, (unsigned long)info->fsr, (unsigned long)info->far, (unsigned long)regs->pc,
            (unsigned long)regs->lr, (unsigned long)regs->sp, (unsigned long)regs->cpsr);
    for (int i = 0; i < 13; i++) fprintf(f, "r%d %08lx\n", i, (unsigned long)regs->r[i]);
    fclose(f);
  }
  if (log_file) { fprintf(log_file, "CRASH pc %08lx lr %08lx\n", (unsigned long)regs->pc, (unsigned long)regs->lr); fclose(log_file); log_file = NULL; }
  svcExitProcess();
}

int main(int argc, char **argv) {
  update_set_launch_path(argc > 0 ? argv[0] : NULL);
  threadOnException(on_exception, exception_stack + sizeof exception_stack, WRITE_DATA_TO_HANDLER_STACK);
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
  dbg_log("Tweebbo %s on %s; linear free %lu KB", APP_VERSION, n3ds ? "New 3DS" : "Old 3DS/2DS",
          (unsigned long)(linearSpaceFree() / 1024));
  if (!gfx_init()) {
    C2D_Fini(); C3D_Fini();
    fatal("Could not load the graphics from romfs.");
    romfsExit(); gfxExit();
    return 1;
  }
  // On New 3DS, 804 MHz gives the avatar compositor plenty of headroom.
  osSetSpeedupEnable(true);
  dbg_log("after gfx_init: linear free %lu KB", (unsigned long)(linearSpaceFree() / 1024));
#ifdef TWEEBBO_DEBUG
  dbg_log("TWEEBBO_LOCAL_DEBUG: build %s, updates from the LAN download page", update_current());
#endif
#ifdef TWEEBBO_SHOWCASE
  dbg_log("TWEEBBO_SHOWCASE: poster build; cast cycles every 5 s");
#endif
  net_start(now_ms());
#ifdef TWEEBBO_SHOWCASE
  showcase_start(now_ms());
#endif

  while (aptMainLoop()) {
    hidScanInput();
    u64 now = now_ms();
    if (!ui_input(hidKeysDown(), hidKeysHeld(), now)) break;
#ifndef TWEEBBO_SHOWCASE
    // Once inside the hotel, check for a newer version, then again every 15 minutes: the offer
    // appears on both screens as soon as a release is published.
    static bool checked;
    static u64 release_check;
    if (!checked && W.in_room && net_online()) { checked = true; release_check = now_ms(); update_check(); }
#ifndef TWEEBBO_DEBUG
    UpdateState rs = update_state();
    if (checked && W.in_room && net_online() && now_ms() - release_check > 15 * 60 * 1000 &&
        (rs == UPD_LATEST || rs == UPD_AVAILABLE || rs == UPD_FAILED)) { release_check = now_ms(); update_check_quiet(); }
#endif
#endif
#ifdef TWEEBBO_DEBUG
    // A pushed debug build reaches a running console within two minutes.
    static u64 last_check;
    if (!last_check && checked) last_check = now_ms();
    UpdateState us = update_state();
    if (checked && W.in_room && net_online() && now_ms() - last_check > 120000 &&
        (us == UPD_LATEST || us == UPD_AVAILABLE || us == UPD_FAILED)) { last_check = now_ms(); update_check_quiet(); }
#endif
    net_update(now);
#ifdef TWEEBBO_SHOWCASE
    showcase_update(now);
#endif
    now = now_ms();

    gfx_frame();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    snow_frame(now);
    text_begin();
    C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(top);
    if (!snow_top(now) && !casino_top(now)) {
      scene_draw(now); gfx_filter(false); shop_top(now); look_top(now); ui_draw_top(now);
#ifdef TWEEBBO_SHOWCASE
      showcase_top(now);
#endif
    }
    C2D_TargetClear(bottom, C2D_Color32(0x1b, 0x25, 0x2c, 255));
    C2D_SceneBegin(bottom);
    gfx_filter(false);
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

  music_exit();
  net_stop();
  snow_close(false);
  conn_global_exit();
  gfx_exit();
  C2D_Fini();
  C3D_Fini();
  romfsExit();
  gfxExit();
  return 0;
}
