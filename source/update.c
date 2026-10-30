#include "update.h"
#include "download.h"
#include "incremental.h"
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
  long size, cia_size, tdsx_size;
  IncrementalOffer small_cia, small_tdsx;
  bool full_fallback;
  char phase[96];
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

static bool sleep_blocked, home_blocked;

UpdateState update_state(void) {
  // Sleep was blocked for the download; give it back on the main thread once it is over.
  if (sleep_blocked && U.state != UPD_DOWNLOADING) { sleep_blocked = false; aptSetSleepAllowed(true); }
  if (home_blocked && U.state != UPD_DOWNLOADING) { home_blocked=false; aptSetHomeAllowed(true); }
  return U.state;
}
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
static volatile bool installing;
const char *update_status(void) { return U.phase; }
bool update_full_fallback(void) { return U.full_fallback; }
bool update_small(void) { const IncrementalOffer *s = U.launch[0] ? &U.small_tdsx : &U.small_cia; long full=U.launch[0]?U.tdsx_size:U.cia_size; return !U.full_fallback && s->bytes && (!full || s->bytes < (unsigned long)full); }
long update_download_bytes(void) { return update_small() ? (long)(U.launch[0]?U.small_tdsx.bytes:U.small_cia.bytes) : U.launch[0]?U.tdsx_size:U.cia_size; }
long update_full_bytes(void) { return U.launch[0]?U.tdsx_size:U.cia_size; }

/** Downloading fills the bar to 90%; installing from the SD card does the rest. */
float update_progress(void) {
  float p = U.size > 0 ? (float)U.done / (float)U.size : 0;
  if (p > 1) p = 1;
  return installing ? 0.9f + 0.1f * p : 0.9f * p;
}

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
  // GitHub's latest-release redirect is cached for a while after publishing: ask past the cache.
  size_t n = strlen(url);
  snprintf(url + n, (size_t)cap - n, "%ct=%llu", strchr(url, '?') ? '&' : '?', (unsigned long long)osGetTime());
#endif
}

static bool is_sha256(const char *s) {
  if (strlen(s) != 64) return false;
  for (; *s; s++) if (!strchr("0123456789abcdefABCDEF", *s)) return false;
  return true;
}

static bool small_url_allowed(const char *url) {
  if (!url || strchr(url,'\r') || strchr(url,'\n') || strchr(url,'@') || strchr(url,'#') || strstr(url,"/../")) return false;
  const char *prefixes[]={"https://tweebbo.lol/",UPDATE_REPO "/releases/download/"};
  for (unsigned i=0;i<sizeof prefixes/sizeof *prefixes;i++) if (!strncmp(url,prefixes[i],strlen(prefixes[i]))) return true;
#ifdef TWEEBBO_DEBUG
  char base[300];manifest_url(base,sizeof base);char *slash=strrchr(base,'/');if(slash){slash[1]=0;return !strncmp(url,base,strlen(base));}
#endif
  return false;
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
  json_t *j = status == 200 && m.buf ? json_loads(m.buf, JSON_REJECT_DUPLICATES, &jerr) : NULL;
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
    if (strcmp(U.version, version) || strcmp(U.sha, sha)) { offers++; U.full_fallback=false; }
    U.cia_size=size;
    U.tdsx_size=(long)json_integer_value(json_object_get(j,"3dsx_size"));
    if(U.tdsx_size<0 || U.tdsx_size>(long)INCREMENTAL_MAX_BYTES)U.tdsx_size=0;
    incremental_offer(j,APP_VERSION,false,small_url_allowed,&U.small_cia);
    incremental_offer(j,APP_VERSION,true,small_url_allowed,&U.small_tdsx);
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
//
// The CIA (tens of MB) is first downloaded to the SD card, resuming after Wi-Fi drops, and only
// then installed through AM from the file. A flaky connection costs time, never the install.

#define CHUNK (128 * 1024)
#define CIA_PART DATA_DIR "/update.cia"
#define DOWNLOAD_ATTEMPTS 10

typedef struct {
  FILE *file;
  u64 offset;   /* bytes already in the file */
  u8 *buf;      /* pending block, written 128 KB at a time */
  int fill;
  bool write_failed;
  mbedtls_sha256_context sha;
} Sink;

static bool flush_chunk(Sink *s) {
  if (!s->fill) return true;
  if (fwrite(s->buf, 1, (size_t)s->fill, s->file) != (size_t)s->fill) {
    s->write_failed = true;
    dbg_log("update: SD write failed at %llu", (unsigned long long)s->offset);
    return false;
  }
  s->offset += (u64)s->fill;
  s->fill = 0;
  return true;
}

static bool file_sink(const u8 *data, int len, void *ctx) {
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

static bool file_text(const char *path, char *out, int cap) {
  FILE *f = fopen(path, "r");
  if (!f) return false;
  bool ok = fgets(out, cap, f) != NULL;
  fclose(f);
  return ok;
}

/** Downloads `url` into `path` until it is complete with SHA-256 `want`. What an earlier attempt
 *  (or an earlier session) left in the file is kept and the download resumes after it; network
 *  errors retry with growing pauses. On failure the partial file stays for the next try. */
static bool download_file(const char *url, const char *path, const char *want, long expect, Sink *s, char *err, int cap) {
  char side[300], had[80] = "";
  snprintf(side, sizeof side, "%s.sha", path);
  // A partial file of another build is useless: start over.
  if (!file_text(side, had, sizeof had) || strcasecmp(had, want)) {
    remove(path);
    FILE *f = fopen(side, "w");
    if (f) { fputs(want, f); fclose(f); }
  }
  mbedtls_sha256_init(&s->sha);
  mbedtls_sha256_starts_ret(&s->sha, 0);
  s->offset = 0;
  s->fill = 0;
  s->write_failed = false;
  FILE *old = fopen(path, "rb");
  if (old) {
    size_t n;
    while ((n = fread(s->buf, 1, CHUNK, old)) > 0) { mbedtls_sha256_update_ret(&s->sha, s->buf, n); s->offset += n; }
    fclose(old);
    if (s->offset) dbg_log("update: resuming at %llu", (unsigned long long)s->offset);
  }
  U.done = (long)s->offset;
  s->file = fopen(path, "ab");
  if (s->file) setvbuf(s->file, NULL, _IONBF, 0);
  bool ok = false;
  if (!s->file) snprintf(err, (size_t)cap, "Could not save the update on the SD card. Is it full or locked?");
  for (int attempt = 0; s->file && attempt < DOWNLOAD_ATTEMPTS; attempt++) {
    if (attempt) svcSleepThread((attempt < 5 ? attempt * 2 : 10) * 1000000000LL);
    // Already complete (a session that ended right before installing): nothing left to fetch.
    if (expect > 0 && (long)s->offset >= expect) { ok = true; break; }
    long total;
    err[0] = 0;
    int status = http_download_optimized_from(url, (long)s->offset, 2, file_sink, s, &total, err, cap);
    if (total > 0) U.size = expect = total;
    bool flushed = flush_chunk(s);
    if (fflush(s->file)) s->write_failed = true;
    dbg_log("update: attempt %d status %d, %llu bytes, err '%s'", attempt, status, (unsigned long long)s->offset, err);
    if (!flushed || s->write_failed) { snprintf(err, (size_t)cap, "Could not save the update on the SD card. Is it full?"); break; }
    if (status == 200 || status == 206) { ok = true; break; }
    if (status == 416) { ok = true; break; }  // nothing after our offset: the checksum decides
    if (status == 403 || status == 404 || status == 410) break;  // gone: re-read the list instead
  }
  if (s->file) { fclose(s->file); s->file = NULL; }
  u8 digest[32];
  mbedtls_sha256_finish_ret(&s->sha, digest);
  mbedtls_sha256_free(&s->sha);
  if (!ok) {
    if (!err[0]) snprintf(err, (size_t)cap, "The download was interrupted.");
    return false;
  }
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  if (strcasecmp(hex, want)) {
    dbg_log("update: checksum mismatch (%s)", hex);
    remove(path);
    remove(side);
    snprintf(err, (size_t)cap, "The download is damaged (checksum mismatch).");
    return false;
  }
  return true;
}

static void forget_file(const char *path) {
  char side[300];
  snprintf(side, sizeof side, "%s.sha", path);
  remove(path);
  remove(side);
}

/** Installs the verified CIA file through AM, the way FBI does (128 KB blocks). */
static Result install_cia(const char *path, u8 *buf) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  Handle h = 0;
  Result r = AM_StartCiaInstall(MEDIATYPE_SD, &h);
  dbg_log("update: AM start 0x%08lX", (unsigned long)r);
  u64 offset = 0;
  U.done = 0;
  while (R_SUCCEEDED(r)) {
    size_t n = fread(buf, 1, CHUNK, f);
    if (!n) { if(ferror(f))r=-1; break; }
    u32 written = 0;
    r = FSFILE_Write(h, &written, offset, buf, (u32)n, 0);
    if (R_SUCCEEDED(r) && written != n) r = -1;
    if (R_FAILED(r)) dbg_log("update: AM write failed at %llu (0x%08lX)", (unsigned long long)offset, (unsigned long)r);
    offset += n;
    U.done = (long)offset;
  }
  if(R_SUCCEEDED(r)&&U.size>0&&offset!=(u64)U.size)r=-1;
  fclose(f);
  if (h) {
    Result fr = R_SUCCEEDED(r) ? AM_FinishCiaInstall(h) : AM_CancelCIAInstall(h);
    dbg_log("update: AM %s 0x%08lX", R_SUCCEEDED(r) ? "finish" : "cancel", (unsigned long)fr);
    if (R_SUCCEEDED(r)) r = fr;
  }
  return r;
}

static void rebuild_progress(unsigned done, void *unused) { (void)unused; U.done=(long)done; }

static bool replace_prepared(const char *from,const char *to) {
  if(!rename(from,to))return true;
  char backup[320];snprintf(backup,sizeof backup,"%s.bak",to);remove(backup);
  if(rename(to,backup))return false;
  if(!rename(from,to))return true;
  rename(backup,to);return false;
}

static bool prepare_small(const char *path,const IncrementalOffer *offer,bool homebrew,Sink *sink,char *error,int cap) {
  char patch[288],building[320],side[320];
  snprintf(patch,sizeof patch,DATA_DIR "/update-%s.tupd",homebrew?"3dsx":"cia");
  snprintf(building,sizeof building,"%s.rebuilding",path);
  U.size=offer->bytes;U.done=0;snprintf(U.phase,sizeof U.phase,"Downloading the smaller update...");
  if(!download_file(offer->url,patch,offer->sha256,offer->bytes,sink,error,cap))return false;
  U.size=homebrew?U.tdsx_size:U.cia_size;U.done=0;
  snprintf(U.phase,sizeof U.phase,"Preparing from installed files...");
  char title[17];snprintf(title,sizeof title,"%016llX",(unsigned long long)TITLE_ID);
  IncrementalTarget target={title,APP_VERSION,U.version,homebrew?U.tdsx_sha:U.sha,(unsigned)U.size,homebrew};
  IncrementalHooks hooks={NULL,rebuild_progress,NULL};
  IncrementalResult result=incremental_apply(patch,building,"romfs:/",offer,&target,&hooks,error,(size_t)cap);
  if(result!=INCREMENTAL_OK){
    if(result!=INCREMENTAL_IO_ERROR&&result!=INCREMENTAL_CANCELLED){
      U.full_fallback=true;
      if(U.size>0)snprintf(error,(size_t)cap,"Small update unavailable. Full download: %.1f MB. Choose Full download to continue.",U.size/1000000.0);
      else snprintf(error,(size_t)cap,"Small update unavailable. Choose Full download to continue.");
    }
    return false;
  }
  if(!replace_prepared(building,path)){snprintf(error,(size_t)cap,"Could not save the prepared update on the SD card.");return false;}
  snprintf(side,sizeof side,"%s.sha",path);FILE *f=fopen(side,"w");bool ok=f&&fputs(homebrew?U.tdsx_sha:U.sha,f)>=0;if(f&&fclose(f))ok=false;
  if(!ok)snprintf(error,(size_t)cap,"Could not save the update checksum on the SD card.");
  struct stat info;if(ok&&(!stat(path,&info)))U.size=(long)info.st_size;
  return ok;
}

static bool package_identity(const char *path,bool homebrew) {
  FILE *f=fopen(path,"rb");if(!f)return false;unsigned char h[32];bool ok=fread(h,1,sizeof h,f)==sizeof h;
  if(homebrew)ok=ok&&!memcmp(h,"3DSX",4);
  else if(ok){
    unsigned length=(unsigned)h[0]|(unsigned)h[1]<<8|(unsigned)h[2]<<16|(unsigned)h[3]<<24;
    ok=length>=32&&length<=65536;
    unsigned long long tmd=0;
    for(unsigned i=0;ok&&i<3;i++){unsigned at=(unsigned[]){0,8,12}[i];unsigned n=(unsigned)h[at]|(unsigned)h[at+1]<<8|(unsigned)h[at+2]<<16|(unsigned)h[at+3]<<24;tmd+=((unsigned long long)n+63)&~63ull;}
    if(ok)ok=tmd<=INCREMENTAL_MAX_BYTES-0x194&&!fseek(f,(long)tmd,SEEK_SET)&&fread(h,1,4,f)==4&&!memcmp(h,"\x00\x01\x00\x04",4);
    if(ok)ok=!fseek(f,(long)(tmd+0x140+0x4c),SEEK_SET)&&fread(h,1,8,f)==8;
    unsigned long long title=0;for(unsigned i=0;ok&&i<8;i++)title=title<<8|h[i];if(ok)ok=title==TITLE_ID;
  }
  fclose(f);return ok;
}

static void install_thread(void *arg) {
  (void)arg;
  UpdateState fresh=read_manifest();
  if(fresh!=UPD_AVAILABLE){U.state=fresh==UPD_LATEST?UPD_LATEST:UPD_FAILED;return;}
  bool homebrew=U.launch[0]!=0,ok=false;
  char error[160]="",path[280];Sink sink={0};sink.buf=malloc(CHUNK);
  if(!sink.buf){snprintf(U.error,sizeof U.error,"Not enough memory to update.");U.state=UPD_FAILED;return;}
  if(homebrew)snprintf(path,sizeof path,"%s.new",U.launch);
  else{mkdir("sdmc:/3ds",0777);mkdir(DATA_DIR,0777);snprintf(path,sizeof path,"%s",CIA_PART);}
  const char *want=homebrew?U.tdsx_sha:U.sha;long full=homebrew?U.tdsx_size:U.cia_size;
  char side[320],had[80]="";snprintf(side,sizeof side,"%s.sha",path);struct stat info;
  bool complete=full>0&&file_text(side,had,sizeof had)&&!strcasecmp(had,want)&&!stat(path,&info)&&info.st_size==full;
  if(update_small()&&!complete){
    const IncrementalOffer *offer=homebrew?&U.small_tdsx:&U.small_cia;
    ok=prepare_small(path,offer,homebrew,&sink,error,sizeof error);
  }else{
    snprintf(U.phase,sizeof U.phase,"Downloading the full update...");
    for(int round=0;round<2&&!ok;round++){
      const char *url=homebrew?U.tdsx:U.cia;want=homebrew?U.tdsx_sha:U.sha;
      if(!url[0]){snprintf(error,sizeof error,"No download for this kind of install.");break;}
      U.size=homebrew?U.tdsx_size:U.cia_size;
      ok=download_file(url,path,want,U.size,&sink,error,sizeof error);
      if(!ok&&(round>0||read_manifest()!=UPD_AVAILABLE))break;
    }
  }
  if(ok&&!package_identity(path,homebrew)){ok=false;snprintf(error,sizeof error,"The update is not a compatible Tweebbo package. Your current app is kept.");}
  if(ok&&homebrew){
    snprintf(U.phase,sizeof U.phase,"Saving the verified app...");
    ok=replace_prepared(path,U.launch);
    if(ok)forget_file(path);else snprintf(error,sizeof error,"Could not replace the app. Your previous .3dsx is kept.");
  }else if(ok){
    installing=true;snprintf(U.phase,sizeof U.phase,"Installing the verified update...");
    Result result=amInit();
    if(R_SUCCEEDED(result)){result=install_cia(path,sink.buf);if(R_FAILED(result)){svcSleepThread(2000000000LL);result=install_cia(path,sink.buf);}amExit();}
    ok=R_SUCCEEDED(result);
    if(ok)forget_file(path);
    else{
      mkdir("sdmc:/cias",0777);char cia[96];snprintf(cia,sizeof cia,"sdmc:/cias/tweebbo-%s.cia",U.version);remove(cia);
      if(!rename(path,cia)){forget_file(path);snprintf(error,sizeof error,"The console refused the install (0x%08lX). Verified update: SD:/cias/tweebbo-%s.cia. Retry with FBI.",(unsigned long)result,U.version);}
      else snprintf(error,sizeof error,"The console refused the install (0x%08lX).",(unsigned long)result);
    }
    installing=false;
  }
  if(ok){char patch[288];snprintf(patch,sizeof patch,DATA_DIR "/update-%s.tupd",homebrew?"3dsx":"cia");forget_file(patch);}
  free(sink.buf);
  dbg_log("update %s: %s %s",U.version,ok?"installed":"failed",ok?"":error);
  if(!ok)snprintf(U.error,sizeof U.error,"%s",error[0]?error:"Download failed. Choose Update again to resume.");
  U.state=ok?UPD_INSTALLED:UPD_FAILED;
}

void update_start(void) {
  if (U.state != UPD_AVAILABLE && U.state != UPD_FAILED) return;
  if (U.thread) { threadJoin(U.thread, U64_MAX); threadFree(U.thread); U.thread = NULL; }
  U.done = 0;
  U.error[0] = 0;
  snprintf(U.phase,sizeof U.phase,"Preparing the update...");
  U.state = UPD_DOWNLOADING;
  // Closing the lid would put the console to sleep and cut the Wi-Fi mid-download.
  if (!sleep_blocked && aptIsSleepAllowed()) { aptSetSleepAllowed(false); sleep_blocked = true; }
  if (!home_blocked) { aptSetHomeAllowed(false); home_blocked=true; }
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
