#include "update.h"
#include <3ds.h>
#include <jansson.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <strings.h>
#include "net.h"

#ifdef TWEEBBO_DEBUG
// The debug title installs beside the public one and updates from the PC that serves it (port 8097).
#define TITLE_ID 0x0004000007EBB100ULL
#define DEBUG_DOWNLOADS_PORT 8097
#else
#define TITLE_ID 0x0004000007EBB000ULL
#endif

static struct {
  volatile UpdateState state;
  char version[24], notes[400], error[160], cia[400], tdsx[400], sha[72], tdsx_sha[72];
  long size;
  volatile long done;
  char launch[256];
  Thread thread;
} U;

void update_set_launch_path(const char *argv0) {
  // A .3dsx starts from the SD card ("sdmc:/3ds/…"); the installed CIA does not.
  snprintf(U.launch, sizeof U.launch, "%s", argv0 && strstr(argv0, ".3dsx") ? argv0 : "");
}

int version_cmp(const char *a, const char *b) {
  int x[3] = {0}, y[3] = {0};
  sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] - y[i];
  return 0;
}

UpdateState update_state(void) { return U.state; }
const char *update_version(void) { return U.version; }

#ifdef TWEEBBO_DEBUG
static int debug_build(void) {
  static int n = -1;
  if (n < 0) {
    FILE *f = fopen("romfs:/debug-build.txt", "r");
    n = 0;
    if (f) { if (fscanf(f, "%d", &n) != 1) n = 0; fclose(f); }
  }
  return n;
}
#endif

const char *update_current(void) {
#ifdef TWEEBBO_DEBUG
  static char label[40];
  snprintf(label, sizeof label, "%s.%d", APP_VERSION, debug_build());
  return label;
#else
  return APP_VERSION;
#endif
}
const char *update_notes(void) { return U.notes; }
const char *update_error(void) { return U.error; }
float update_progress(void) { return U.size > 0 ? (float)U.done / (float)U.size : 0; }

/** http(s) form of the configured server (ws→http, wss→https); offline: production. */
static void base_url(char *out, int cap) {
  const char *s = net_server();
  if (!strcmp(s, "offline")) s = PROD_SERVER;
  if (!strncmp(s, "wss://", 6)) snprintf(out, (size_t)cap, "https://%s", s + 6);
  else if (!strncmp(s, "ws://", 5)) snprintf(out, (size_t)cap, "http://%s", s + 5);
  else snprintf(out, (size_t)cap, "%s", s);
  size_t n = strlen(out);
  if (n && out[n - 1] == '/') out[n - 1] = 0;
}

typedef struct { char *buf; int len, cap; } Mem;

static bool to_memory(const u8 *data, int len, void *ctx) {
  Mem *m = ctx;
  if (m->len + len + 1 > m->cap) return false;
  memcpy(m->buf + m->len, data, (size_t)len);
  m->len += len;
  m->buf[m->len] = 0;
  return true;
}

static void manifest_url(char *url, int cap) {
#ifdef TWEEBBO_DEBUG
  // ws://192.168.1.20:8092 -> http://192.168.1.20:8097/latest.json (the LAN download page).
  char host[128] = "";
  const char *s = net_server(), *start = strstr(s, "://");
  start = start ? start + 3 : s;
  size_t n = strcspn(start, ":/");
  if (n >= sizeof host) n = sizeof host - 1;
  memcpy(host, start, n); host[n] = 0;
  snprintf(url, (size_t)cap, "http://%s:%d/latest.json", host, DEBUG_DOWNLOADS_PORT);
#else
  if (!store_load("update-url", url, cap) || !url[0]) snprintf(url, (size_t)cap, "%s", UPDATE_MANIFEST);
#endif
}

static bool is_sha256(const char *s) {
  if (strlen(s) != 64) return false;
  for (; *s; s++) if (!strchr("0123456789abcdefABCDEF", *s)) return false;
  return true;
}

static volatile int offers;

/** Reads the manifest and decides LATEST / AVAILABLE / FAILED. Nothing is offered unless the
 *  manifest is complete (URL, size, SHA-256) and newer than the running build. */
static UpdateState read_manifest(void) {
  char url[300];
  manifest_url(url, sizeof url);
  Mem m = {malloc(16384), 0, 16384};
  long total;
  char err[128] = "";
  int status = 0;
  // GitHub occasionally answers a redirect badly: retry a few times before giving up.
  for (int attempt = 0; attempt < 3 && m.buf && status != 200 && status != 404; attempt++) {
    if (attempt) svcSleepThread(1500000000LL);
    m.len = 0;
    status = http_download(url, to_memory, &m, &total, err, sizeof err);
  }
  json_error_t jerr;
  json_t *j = status == 200 && m.buf ? json_loads(m.buf, 0, &jerr) : NULL;
  free(m.buf);
  if (!j) {
    snprintf(U.error, sizeof U.error, "%s", status == 404 ? "No updates published yet." : err[0] ? err : "Could not read the update list.");
    dbg_log("update check %s: %d %s", url, status, U.error);
    return status == 404 ? UPD_LATEST : UPD_FAILED;
  }
  const char *version = json_string_value(json_object_get(j, "version")) ?: "0";
  const char *sha = json_string_value(json_object_get(j, "sha256")) ?: "", *tdsx_sha = json_string_value(json_object_get(j, "3dsx_sha256")) ?: "";
  const char *cia = json_string_value(json_object_get(j, "cia")) ?: "", *tdsx = json_string_value(json_object_get(j, "3dsx")) ?: "";
  long size = (long)json_integer_value(json_object_get(j, "size"));
#ifdef TWEEBBO_DEBUG
  int latest = (int)json_integer_value(json_object_get(j, "build")), running = debug_build();
  // Without a readable build number the console cannot tell old from new: never offer in a loop.
  bool newer = running > 0 && latest > running;
  dbg_log("update check: running %s, latest build %d", update_current(), latest);
#else
  bool newer = version_cmp(version, APP_VERSION) > 0;
  dbg_log("update check: running %s, latest %s", APP_VERSION, version);
#endif
  bool homebrew = U.launch[0] != 0;
  bool complete = homebrew ? tdsx[0] && is_sha256(tdsx_sha) : cia[0] && is_sha256(sha) && size > 0;
  UpdateState state = !newer ? UPD_LATEST : complete ? UPD_AVAILABLE : UPD_FAILED;
  if (newer && !complete) {
    snprintf(U.error, sizeof U.error, "The update list is incomplete. Try again in a minute.");
    dbg_log("update check: incomplete manifest for %s", version);
  }
  if (newer) {
    if (strcmp(U.version, version) || strcmp(U.sha, sha)) offers++;
    snprintf(U.version, sizeof U.version, "%s", version);
    snprintf(U.notes, sizeof U.notes, "%s", json_string_value(json_object_get(j, "notes")) ?: "");
    snprintf(U.sha, sizeof U.sha, "%s", sha);
    snprintf(U.tdsx_sha, sizeof U.tdsx_sha, "%s", tdsx_sha);
    U.size = size;
    // Relative URLs are relative to the server.
    char base[300];
    base_url(base, sizeof base);
    snprintf(U.cia, sizeof U.cia, "%s%s", cia[0] == '/' ? base : "", cia);
    snprintf(U.tdsx, sizeof U.tdsx, "%s%s", tdsx[0] == '/' ? base : "", tdsx);
  } else U.version[0] = 0;
  json_decref(j);
  return state;
}

int update_offer(void) { return offers; }

static void check_thread(void *arg) {
  bool quiet = arg != NULL;
  UpdateState st = read_manifest();
  // A background check that fails keeps the previous answer instead of showing an error.
  if (quiet && st == UPD_FAILED) { U.state = U.version[0] ? UPD_AVAILABLE : UPD_LATEST; return; }
  U.state = st;
}

static void start_check(bool quiet) {
  if (U.state == UPD_CHECKING || U.state == UPD_DOWNLOADING || U.state == UPD_INSTALLED) return;
  if (U.thread) { threadJoin(U.thread, U64_MAX); threadFree(U.thread); U.thread = NULL; }
  if (!quiet) { U.state = UPD_CHECKING; U.error[0] = 0; }
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  U.thread = threadCreate(check_thread, quiet ? (void *)1 : NULL, 32 * 1024, prio + 1, -2, false);
}

void update_check(void) { start_check(false); }
/** Background check: the state only changes when the answer changes (no flashing dialogs). */
void update_check_quiet(void) { start_check(true); }

// ---------------------------------------------------------------- installation

#define CHUNK (128 * 1024)

typedef struct {
  Handle cia;   /* AM install in progress, or 0 */
  FILE *file;   /* file on the SD card (.3dsx or fallback CIA), or NULL */
  u64 offset;   /* bytes already written */
  u8 *buf;      /* pending block: AM receives 128 KB blocks, like FBI */
  int fill;
  Result fail;
  mbedtls_sha256_context sha;
} Sink;

static bool flush_chunk(Sink *s) {
  if (!s->fill) return true;
  if (s->cia) {
    u32 written = 0;
    Result r = FSFILE_Write(s->cia, &written, s->offset, s->buf, (u32)s->fill, 0);
    if (R_FAILED(r) || written != (u32)s->fill) {
      s->fail = R_FAILED(r) ? r : -1;
      dbg_log("update: AM write failed at %llu (0x%08lX, %lu/%d)", (unsigned long long)s->offset, (unsigned long)r,
              (unsigned long)written, s->fill);
      return false;
    }
  } else if (s->file && fwrite(s->buf, 1, (size_t)s->fill, s->file) != (size_t)s->fill) {
    s->fail = -2;
    dbg_log("update: SD write failed at %llu", (unsigned long long)s->offset);
    return false;
  }
  s->offset += (u64)s->fill;
  s->fill = 0;
  return true;
}

static bool install_sink(const u8 *data, int len, void *ctx) {
  Sink *s = ctx;
  mbedtls_sha256_update_ret(&s->sha, data, (size_t)len);
  while (len > 0) {
    int take = CHUNK - s->fill < len ? CHUNK - s->fill : len;
    memcpy(s->buf + s->fill, data, (size_t)take);
    s->fill += take;
    data += take;
    len -= take;
    U.done += take;
    if (s->fill == CHUNK && !flush_chunk(s)) return false;
  }
  return true;
}

/** Downloads `url` into the sink; true when it arrived complete with the expected SHA-256. */
static bool fetch(const char *url, const char *want, Sink *s, char *err, int cap) {
  mbedtls_sha256_init(&s->sha);
  mbedtls_sha256_starts_ret(&s->sha, 0);
  U.done = 0;
  long total;
  int status = http_download(url, install_sink, s, &total, err, cap);
  if (total > 0) U.size = total;
  bool ok = status == 200 && flush_chunk(s);
  u8 digest[32];
  mbedtls_sha256_finish_ret(&s->sha, digest);
  mbedtls_sha256_free(&s->sha);
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  dbg_log("update: download %s status %d, %llu bytes, err '%s'", url, status, (unsigned long long)(s->offset), err);
  if (ok && want[0] && strcasecmp(hex, want)) { snprintf(err, (size_t)cap, "The download is damaged (checksum mismatch)."); ok = false; }
  if (!ok && !err[0]) snprintf(err, (size_t)cap, s->fail ? "Could not write the update (0x%08lX)." : "The download was interrupted.", (unsigned long)s->fail);
  return ok;
}

static void install_thread(void *arg) {
  (void)arg;
  // Re-read the manifest first: a build published after the offer must not be checked
  // against the old SHA-256 (it would look damaged).
  UpdateState fresh = read_manifest();
  if (fresh != UPD_AVAILABLE) {
    if (fresh == UPD_LATEST) { U.state = UPD_LATEST; return; }
    U.state = UPD_FAILED;
    return;
  }
  bool homebrew = U.launch[0] != 0;
  const char *url = homebrew ? U.tdsx : U.cia;
  char err[160] = "";
  bool ok = false;
  Sink s = {0};
  s.buf = malloc(CHUNK);
  if (!s.buf) { snprintf(U.error, sizeof U.error, "Not enough memory to update."); U.state = UPD_FAILED; return; }
  if (!url[0]) snprintf(err, sizeof err, "No download for this kind of install.");
  else if (homebrew) {
    char tmp[280];
    snprintf(tmp, sizeof tmp, "%s.new", U.launch);
    s.file = fopen(tmp, "wb");
    if (!s.file) snprintf(err, sizeof err, "Could not write to the SD card.");
    else {
      ok = fetch(url, U.tdsx_sha, &s, err, sizeof err);
      fclose(s.file);
      if (ok) { remove(U.launch); ok = rename(tmp, U.launch) == 0; if (!ok) snprintf(err, sizeof err, "Could not replace the .3dsx file."); }
      else remove(tmp);
    }
  } else {
    Result r = amInit();
    if (R_SUCCEEDED(r)) r = AM_StartCiaInstall(MEDIATYPE_SD, &s.cia);
    dbg_log("update: AM start 0x%08lX", (unsigned long)r);
    for (int attempt = 0; attempt < 3 && R_SUCCEEDED(r) && !ok; attempt++) {
      if (attempt) {
        // Retry an interrupted download with a fresh install.
        svcSleepThread(2000000000LL);
        s.offset = 0; s.fill = 0; s.fail = 0;
        err[0] = 0;
        r = AM_StartCiaInstall(MEDIATYPE_SD, &s.cia);
        dbg_log("update: retry %d, AM start 0x%08lX", attempt, (unsigned long)r);
        if (R_FAILED(r)) break;
      }
      ok = fetch(url, U.sha, &s, err, sizeof err);
      Result fr = ok ? AM_FinishCiaInstall(s.cia) : AM_CancelCIAInstall(s.cia);
      dbg_log("update: AM %s 0x%08lX", ok ? "finish" : "cancel", (unsigned long)fr);
      if (ok && R_FAILED(fr)) { ok = false; snprintf(err, sizeof err, "Install failed (0x%08lX).", (unsigned long)fr); break; }
      // A build published during the download fails the checksum: re-read the list once and retry.
      if (strstr(err, "damaged") && (attempt > 0 || read_manifest() != UPD_AVAILABLE)) break;
    }
    if (R_FAILED(r)) snprintf(err, sizeof err, "The console refused the install (0x%08lX).", (unsigned long)r);
    amExit();
    if (!ok && strstr(err, "damaged") == NULL && strstr(err, "reach") == NULL) {
      // Fallback: save the CIA on the SD card to install it with FBI.
      mkdir("sdmc:/cias", 0777);
      char path[96];
      snprintf(path, sizeof path, "sdmc:/cias/tweebbo-%s.cia", U.version);
      Sink f = {0};
      f.buf = s.buf;
      f.file = fopen(path, "wb");
      char err2[160] = "";
      if (f.file) {
        bool saved = fetch(url, U.sha, &f, err2, sizeof err2);
        fclose(f.file);
        if (saved) snprintf(err, sizeof err, "Could not install automatically. Saved to SD:/cias/tweebbo-%s.cia: install it with FBI.", U.version);
        else remove(path);
      }
    }
  }
  free(s.buf);
  dbg_log("update %s: %s %s", U.version, ok ? "installed" : "failed", ok ? "" : err);
  if (!ok) snprintf(U.error, sizeof U.error, "%s", err[0] ? err : "Download failed.");
  U.state = ok ? UPD_INSTALLED : UPD_FAILED;
}

void update_start(void) {
  if (U.state != UPD_AVAILABLE && U.state != UPD_FAILED) return;
  if (U.thread) { threadJoin(U.thread, U64_MAX); threadFree(U.thread); U.thread = NULL; }
  U.done = 0;
  U.error[0] = 0;
  U.state = UPD_DOWNLOADING;
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  U.thread = threadCreate(install_thread, NULL, 48 * 1024, prio + 1, -2, false);
}

bool update_restart(void) {
  if (U.launch[0]) return true;  // .3dsx: the Homebrew Launcher opens it again
  // CIA: jump to the updated application itself.
  u8 hmac[0x20] = {0};
  if (R_SUCCEEDED(APT_PrepareToDoApplicationJump(0, TITLE_ID, MEDIATYPE_SD))) APT_DoApplicationJump(NULL, 0, hmac);
  return true;
}
