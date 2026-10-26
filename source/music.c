#include "language.h"
// Room playlist and native Trax playback (see music.h). The server owns the queue and the
// shared position; this file follows it like client/src/music.ts and trax.ts: four channels of
// original samples on two-second slots (SoundMachine Component Class).
#include "music.h"
#include "ui_int.h"
#include "assets.h"
#include "gfx.h"
#include "net.h"
#include "world.h"
#include "ws.h"
#include <3ds.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_TRACKS 50
#define MAX_CLIPS 150
#define MAX_SONGS 50
#define MAX_SAMPLE 1024
#define RATE 16000
#define SLOT_MS 2000
#define FIRST_CHANNEL 8
#define CACHE_DIR "sdmc:/3ds/Tweebbo/trax"
#define MUSIC_RESERVE (4u * 1024 * 1024)

typedef struct { u16 sample, start; } Clip;
typedef struct { char provider[12], title[80]; int duration, slots; u8 clip_n[4]; Clip clips[4][MAX_CLIPS]; } Track;
typedef struct { u32 id; char title[80]; } SavedSong;
typedef struct { s8 *data; u32 n; volatile u8 state; } Sample;  /* 0 none, 1 queued, 2 ready, 3 failed */
enum { S_NONE, S_QUEUED, S_READY, S_FAILED };

static struct {
  bool known, playing, listening;
  s64 offset, started, position;
  u64 revision;
  u32 machine, active;          /* item whose queue is shown, item playing (0 none) */
  int index, track_n, song_n;
  Track tracks[MAX_TRACKS];
  SavedSong songs[MAX_SONGS];
  ListState list;
  /* Playback: which clip each channel plays, for the track and revision being heard. */
  int clip_on[4];
  u64 heard_key;
  bool dsp;
  ndspWaveBuf buf[4];
  char line[120];
} M = {.listening = true};

static Sample samples[MAX_SAMPLE];
static Thread worker;
static LightEvent wake;
static volatile bool quit;

bool music_machine(u16 def) {
  for (int i = 0; i < music_machine_count; i++) if (music_machines[i] == def) return true;
  return false;
}

// ---------------------------------------------------------------- samples

typedef struct { u8 *buf; int len, cap; } Grow;
static bool to_grow(const u8 *data, int len, void *ctx) {
  Grow *g = ctx;
  if (g->len + len > 512 * 1024) return false;
  if (g->len + len > g->cap) {
    int cap = g->cap ? g->cap * 2 : 65536;
    while (cap < g->len + len) cap *= 2;
    u8 *p = realloc(g->buf, (size_t)cap);
    if (!p) return false;
    g->buf = p; g->cap = cap;
  }
  memcpy(g->buf + g->len, data, (size_t)len);
  g->len += len;
  return true;
}

/** http(s) form of the hotel the console plays on (ws→http, wss→https). */
static bool base_url(char *out, int cap) {
  const char *s = net_server();
  if (!strncmp(s, "wss://", 6)) snprintf(out, (size_t)cap, "https://%s", s + 6);
  else if (!strncmp(s, "ws://", 5)) snprintf(out, (size_t)cap, "http://%s", s + 5);
  else return false;
  size_t n = strlen(out);
  if (n && out[n - 1] == '/') out[n - 1] = 0;
  return true;
}

static void load_sample(int id) {
  char path[64], url[192], err[96] = "";
  snprintf(path, sizeof path, CACHE_DIR "/%d.pcm", id);
  Grow g = {0};
  FILE *f = fopen(path, "rb");
  if (f) {
    u8 chunk[4096]; size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0 && to_grow(chunk, (int)n, &g)) {}
    fclose(f);
  }
  if (!g.len) {
    long total = 0;
    if (base_url(url, sizeof url - 32)) {
      strcat(url, "/static/trax/3ds/");
      snprintf(url + strlen(url), sizeof url - strlen(url), "%d.pcm", id);
      int status = http_download(url, to_grow, &g, &total, err, sizeof err);
      if (status != 200 || (total > 0 && total != g.len)) g.len = 0;
      if (g.len) {
        mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/Tweebbo", 0777); mkdir(CACHE_DIR, 0777);
        FILE *out = fopen(path, "wb");
        if (out) { fwrite(g.buf, 1, (size_t)g.len, out); fclose(out); }
      }
    }
  }
  // Samples live in linear memory (the DSP reads them), which room textures also need: never
  // let the music take the last MUSIC_RESERVE bytes.
  s8 *data = g.len && linearSpaceFree() > (u32)g.len + MUSIC_RESERVE ? linearAlloc((size_t)g.len) : NULL;
  if (g.len && !data) dbg_log("music: sample %d skipped (linear free %lu KB)", id, (unsigned long)(linearSpaceFree() / 1024));
  if (data) { memcpy(data, g.buf, (size_t)g.len); DSP_FlushDataCache(data, (u32)g.len); }
  free(g.buf);
  samples[id].data = data;
  samples[id].n = data ? (u32)g.len : 0;
  __sync_synchronize();
  samples[id].state = data ? S_READY : S_FAILED;
}

static void work(void *arg) {
  (void)arg;
  while (!quit) {
    LightEvent_Wait(&wake);
    // Rescan until nothing waits: samples queued behind the scan are not left for the next signal.
    for (bool any = true; any && !quit;) {
      any = false;
      for (int id = 1; id < MAX_SAMPLE && !quit; id++) if (samples[id].state == S_QUEUED) { load_sample(id); any = true; }
    }
  }
}

static void want(int id) {
  if (id <= 0 || id >= MAX_SAMPLE || samples[id].state != S_NONE) return;
  samples[id].state = S_QUEUED;
  if (!worker) {
    LightEvent_Init(&wake, RESET_ONESHOT);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    worker = threadCreate(work, NULL, 32 * 1024, prio + 1, -2, false);
  }
  LightEvent_Signal(&wake);
}

// ---------------------------------------------------------------- state

static int num(json_t *o, const char *k) { return (int)json_integer_value(json_object_get(o, k)); }

static void read_track(json_t *v, Track *t) {
  memset(t, 0, sizeof *t);
  utf8_copy(t->provider, sizeof t->provider, json_string_value(json_object_get(v, "provider")));
  utf8_copy(t->title, sizeof t->title, json_string_value(json_object_get(v, "title")));
  t->duration = num(v, "duration");
  json_t *song = json_object_get(v, "song");
  t->slots = num(song, "slots");
  size_t c; json_t *channel;
  json_array_foreach(json_object_get(song, "channels"), c, channel) {
    if (c >= 4) break;
    size_t i; json_t *clip;
    json_array_foreach(channel, i, clip) {
      if (t->clip_n[c] >= MAX_CLIPS) break;
      t->clips[c][t->clip_n[c]++] = (Clip){(u16)num(clip, "sample"), (u16)num(clip, "start")};
    }
  }
}

void music_receive(const char *text, u64 now) {
  json_t *j = json_loads(text, 0, NULL), *st = json_object_get(j, "state");
  if (!json_is_object(st)) { json_decref(j); return; }
  M.known = true;
  M.offset = (s64)json_integer_value(json_object_get(j, "now")) - (s64)now;
  json_t *active = json_object_get(st, "active");
  M.active = json_is_integer(active) ? (u32)json_integer_value(active) : 0;
  M.index = num(st, "index");
  M.playing = json_is_true(json_object_get(st, "playing"));
  M.started = (s64)json_integer_value(json_object_get(st, "started"));
  M.position = (s64)json_integer_value(json_object_get(st, "position"));
  M.revision = (u64)json_integer_value(json_object_get(st, "revision"));
  // The queue shown: the machine playing, else the first one with tracks.
  json_t *queues = json_object_get(st, "queues"), *queue = NULL;
  char key[16];
  if (M.active) { snprintf(key, sizeof key, "%u", (unsigned)M.active); queue = json_object_get(queues, key); M.machine = M.active; }
  if (!queue) {
    const char *k; json_t *v;
    json_object_foreach(queues, k, v) if (json_array_size(v)) { queue = v; M.machine = (u32)strtoul(k, NULL, 10); break; }
  }
  M.track_n = 0;
  size_t i; json_t *v;
  json_array_foreach(queue, i, v) if (M.track_n < MAX_TRACKS) read_track(v, &M.tracks[M.track_n++]);
  M.song_n = 0;
  const char *k;
  json_object_foreach(json_object_get(st, "songs"), k, v) {
    if (M.song_n >= MAX_SONGS) break;
    M.songs[M.song_n].id = (u32)strtoul(k, NULL, 10);
    utf8_copy(M.songs[M.song_n++].title, sizeof M.songs[0].title, json_string_value(json_object_get(v, "title")));
  }
  json_decref(j);
}

static void silence(void) {
  for (int c = 0; c < 4; c++) { if (M.dsp) ndspChnWaveBufClear(FIRST_CHANNEL + c); M.clip_on[c] = -1; }
}

/** Frees the samples the given track does not use (all of them for NULL). Call after
 *  silence(): no channel plays them any more. Samples being downloaded are left alone. */
static void release_samples(const Track *keep) {
  for (int id = 1; id < MAX_SAMPLE; id++) {
    if (samples[id].state != S_READY && samples[id].state != S_FAILED) continue;
    bool used = false;
    for (int c = 0; keep && c < 4 && !used; c++)
      for (int i = 0; i < keep->clip_n[c] && !used; i++) used = keep->clips[c][i].sample == id;
    if (used) continue;
    if (samples[id].data) linearFree(samples[id].data);
    samples[id].data = NULL;
    samples[id].n = 0;
    __sync_synchronize();
    samples[id].state = S_NONE;
  }
}

void music_room_changed(void) {
  silence();
  release_samples(NULL);
  M.known = M.playing = false;
  M.active = M.machine = 0;
  M.track_n = M.song_n = 0;
  M.heard_key = 0;
}

static const Track *playing_track(void) {
  if (!M.known || !M.playing || !M.active || M.active != M.machine || M.index < 0 || M.index >= M.track_n) return NULL;
  return &M.tracks[M.index];
}

const char *music_now_playing(void) {
  const Track *t = playing_track();
  if (!t) return "";
  bool trax = !strcmp(t->provider, "trax");
  snprintf(M.line, sizeof M.line, trax ? TR("Now playing: %s") : TR("Now playing: %s (on PC)"), t->title);
  return M.line;
}

// ---------------------------------------------------------------- playback

static bool dsp_ready(void) {
  if (M.dsp) return true;
  if (R_FAILED(ndspInit())) return false;
  for (int c = 0; c < 4; c++) {
    int ch = FIRST_CHANNEL + c;
    ndspChnReset(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetRate(ch, RATE);
    ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM8);
    float mix[12] = {.55f, .55f};
    ndspChnSetMix(ch, mix);
    M.clip_on[c] = -1;
  }
  M.dsp = true;
  return true;
}

void music_poll(u64 now) {
  const Track *t = playing_track();
  if (!t || strcmp(t->provider, "trax") || !M.listening) { if (M.heard_key) { silence(); release_samples(NULL); M.heard_key = 0; } return; }
  u64 key = M.revision * 64 + (u64)M.index;
  if (key != M.heard_key) { silence(); release_samples(t); M.heard_key = key; }
  s64 elapsed = M.position + ((s64)now + M.offset - M.started);
  if (elapsed < 0) elapsed = 0;
  for (int c = 0; c < 4; c++) {
    // Fetch the samples a little ahead of time; the first song may start late while downloading.
    for (int i = 0; i < t->clip_n[c]; i++) want(t->clips[c][i].sample);
  }
  if (!dsp_ready()) return;
  for (int c = 0; c < 4; c++) {
    int found = -1;
    for (int i = 0; i < t->clip_n[c]; i++) {
      const Clip *clip = &t->clips[c][i];
      s64 from = (s64)clip->start * SLOT_MS;
      const Sample *s = clip->sample < MAX_SAMPLE ? &samples[clip->sample] : NULL;
      s64 to = from + (s && s->state == S_READY ? (s64)s->n * 1000 / RATE : SLOT_MS);
      if (elapsed >= from && elapsed < to) { found = i; break; }
    }
    if (found == M.clip_on[c]) continue;
    int ch = FIRST_CHANNEL + c;
    ndspChnWaveBufClear(ch);
    M.clip_on[c] = -1;
    if (found < 0) continue;
    const Clip *clip = &t->clips[c][found];
    Sample *s = &samples[clip->sample];
    if (s->state != S_READY) continue;  // still downloading: joins at the next clip
    u32 skip = (u32)((elapsed - (s64)clip->start * SLOT_MS) * RATE / 1000);
    if (skip >= s->n) continue;
    memset(&M.buf[c], 0, sizeof M.buf[c]);
    M.buf[c].data_vaddr = s->data + skip;
    M.buf[c].nsamples = s->n - skip;
    ndspChnWaveBufAdd(ch, &M.buf[c]);
    M.clip_on[c] = found;
  }
}

void music_exit(void) {
  silence();
  if (M.dsp) { ndspExit(); M.dsp = false; }
  if (worker) { quit = true; LightEvent_Signal(&wake); threadJoin(worker, U64_MAX); threadFree(worker); worker = NULL; }
}

// ---------------------------------------------------------------- "Press A" and the window

bool music_context(char *label, int cap, u64 now) {
  Ent *me = world_ent(W.me);
  if (!me || ent_moving(me, now)) return false;
  for (int i = 0; i < MAX_ITEMS; i++) {
    const Item *it = &W.items[i];
    if (!it->used || !music_machine(it->def) || abs(it->x - me->x) > 1 || abs(it->y - me->y) > 1) continue;
    snprintf(label, cap,"%s",TR("Open the playlist"));
    if (!M.machine) M.machine = it->id;
    return true;
  }
  return false;
}

static void command(const char *op, const char *extra) {
  if (!M.machine) return;
  char json[128];
  snprintf(json, sizeof json, "{\"op\":\"%s\",\"item\":%u%s%s}", op, (unsigned)M.machine, extra ? "," : "", extra ? extra : "");
  send_music(json);
}

void music_draw(u64 now) {
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, TR("Room playlist"))) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  float x = IN_X + 6, y = IN_Y + 4;
  if (!M.known) { send_music("{\"op\":\"sync\"}"); M.known = true; }
  const Track *t = playing_track();
  if (t) {
    bool trax = !strcmp(t->provider, "trax");
    sk_text(t->title, x, y, 11, SK_TEXT, 0, IN_W - 12);
    sk_text(trax ? (M.listening ? TR("Trax · playing here") : TR("Trax · muted on this console"))
                 : !strcmp(t->provider, "youtube") ? TR("YouTube · plays on PC and phones") : TR("Spotify · plays on PC and phones"),
            x, y + 14, 9, SK_DIM, 0, IN_W - 12);
    if (t->duration > 0) {
      s64 at = M.position + ((s64)now + M.offset - M.started);
      float k = (float)at / (float)t->duration;
      k = k < 0 ? 0 : k > 1 ? 1 : k;
      fill_round(x, y + 28, IN_W - 12, 5, 2, sk_rgb(0xc9c9c9));
      fill_round(x, y + 28, (IN_W - 12) * k, 5, 2, sk_rgb(0x4f7486));
    }
  } else sk_text(M.track_n ? TR("Nothing is playing. Choose a track.") : TR("The playlist is empty. Add tracks on PC or below."), x, y + 6, 10, SK_DIM, 0, IN_W - 12);
  float by = y + 38;
  if (sk_button(x, by, 92, M.listening ? TR("Mute (Y)") : TR("Listen (Y)"), true) || K.y) { M.listening = !M.listening; if (!M.listening) { silence(); M.heard_key = 0; } }
  if (t && (sk_button(x + 96, by, 92, TR("Pause (X)"), true) || K.x)) command("pause", NULL);
  if (t && sk_button(x + 192, by, 96, TR("Next track"), true)) command("next", NULL);
  if (!t && M.track_n && (sk_button(x + 96, by, 92, TR("Play (X)"), true) || K.x)) { char e[16]; snprintf(e, sizeof e, "\"index\":%d", M.index < M.track_n ? M.index : 0); command("play", e); }
  // The queue, then the room's saved Trax songs that can be added to it.
  int rows = M.track_n + (M.song_n ? M.song_n + 1 : 0);
  const float row = 22;
  if (K.dn && M.list.focus < rows - 1) M.list.focus++;
  if (K.up && M.list.focus > 0) M.list.focus--;
  int first = sk_list_begin(&M.list, IN_X, by + 26, IN_W, IN_Y + IN_H - by - 26, rows, row);
  for (int i = first; i < rows; i++) {
    float ry = sk_list_row_y(i);
    if (ry > IN_Y + IN_H) break;
    bool focus = M.list.focus == i;
    fill_rect(IN_X + 2, ry, IN_W - 4, row - 1, sk_rgb(focus ? SK_SELECT : i % 2 ? SK_ROW_ALT : SK_ROW));
    if (i < M.track_n) {
      const Track *q = &M.tracks[i];
      char title[96]; snprintf(title, sizeof title, "%s%s", (M.active == M.machine && i == M.index && M.playing) ? "> " : "", q->title);
      sk_text(title, IN_X + 8, ry + 5, 9.5f, SK_TEXT, 0, IN_W - 100);
      sk_text(q->provider, IN_X + IN_W - 92, ry + 6, 8.5f, SK_DIM, 0, 40);
      if (sk_go(IN_X + IN_W - 46, ry + 2, 38, 17, TR("Play"), true) || (focus && K.a)) { char e[16]; snprintf(e, sizeof e, "\"index\":%d", i); command("play", e); }
    } else if (i == M.track_n) {
      sk_text(TR("Room songs (Trax)"), IN_X + 8, ry + 5, 9.5f, SK_DIM, 0, IN_W - 20);
    } else {
      const SavedSong *s = &M.songs[i - M.track_n - 1];
      sk_text(s->title, IN_X + 8, ry + 5, 9.5f, SK_TEXT, 0, IN_W - 60);
      if (sk_go(IN_X + IN_W - 46, ry + 2, 38, 17, TR("Add"), true) || (focus && K.a)) { char e[24]; snprintf(e, sizeof e, "\"song\":%u", (unsigned)s->id); command("add_song", e); }
    }
  }
  sk_list_end();
}
