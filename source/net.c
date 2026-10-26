#include "net.h"
#include "language.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "proto.h"
#include "sim.h"
#include "world.h"

#ifdef __3DS__
#include <3ds.h>
typedef LightLock Mutex;
static void mutex_init(Mutex *m) { LightLock_Init(m); }
static void lock(Mutex *m) { LightLock_Lock(m); }
static void unlock(Mutex *m) { LightLock_Unlock(m); }
static void sleep_ms(int ms) { svcSleepThread((s64)ms * 1000000); }
typedef Thread ThreadHandle;
static ThreadHandle spawn(void (*fn)(void *)) {
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  // Higher priority than the draw loop: the thread spends almost all its time waiting in poll().
  return threadCreate(fn, NULL, 64 * 1024, prio > 0x19 ? prio - 1 : prio, -2, false);
}
static void join(ThreadHandle t) { if (t) { threadJoin(t, U64_MAX); threadFree(t); } }
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_mutex_t Mutex;
static void mutex_init(Mutex *m) { pthread_mutex_init(m, NULL); }
static void lock(Mutex *m) { pthread_mutex_lock(m); }
static void unlock(Mutex *m) { pthread_mutex_unlock(m); }
static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }
typedef pthread_t *ThreadHandle;
static void *trampoline(void *fn) { ((void (*)(void *))fn)(NULL); return NULL; }
static ThreadHandle spawn(void (*fn)(void *)) {
  pthread_t *t = malloc(sizeof *t);
  pthread_create(t, NULL, trampoline, (void *)fn);
  return t;
}
static void join(ThreadHandle t) { if (t) { pthread_join(*t, NULL); free(t); } }
#endif

/** The server closes the WebSocket after 270 s (Vercel limit); renew before that. */
#define RENEW_MS 240000
#define PLATFORM_3DS 1
#define MAX_HTTP 8

typedef struct Msg { struct Msg *next; int len; u8 data[]; } Msg;
typedef struct { Msg *head, *tail; int count; size_t bytes; } Queue;

static bool q_push(Queue *q, const u8 *data, int len) {
  if (len <= 0 || q->count >= 512 || (size_t)len + q->bytes > 1024u * 1024) return false;
  Msg *m = malloc(sizeof *m + (size_t)len);
  if (!m) return false;
  m->next = NULL; m->len = len;
  memcpy(m->data, data, (size_t)len);
  if (q->tail) q->tail->next = m; else q->head = m;
  q->tail = m;
  q->count++; q->bytes += (size_t)len;
  return true;
}

static Msg *q_pop(Queue *q) {
  Msg *m = q->head;
  if (m) { q->head = m->next; if (!q->head) q->tail = NULL; q->count--; q->bytes -= (size_t)m->len; }
  return m;
}

static void q_clear(Queue *q) { for (Msg *m; (m = q_pop(q));) free(m); }

typedef struct {
  int id;
  bool queued, done, prod;
  char method[8], path[160];
  char *json;
  bool auth;
  HttpResult res;
} HttpJob;

static struct {
  bool started, offline, quit, doorbell;
  Mutex mu;
  ThreadHandle thread, http_thread, food_thread;
  bool food_enabled;
  u32 food_gen;
  Queue food_in, food_out;
  char server[128], session_server[128];
  Endpoint ep;
  // Protected by mu:
  char slug[64], arrival[80], token[80], password[65];
  bool welcomed;
  NetState blocked;
  bool room_full;
  u64 retry_at;
  u32 want_gen;
  bool missing_room;
  u32 missing_room_gen;
  NetState state;
  char status[128];
  Queue in, out;
  HttpJob http[MAX_HTTP];
  int next_http;
  bool token_saved;
  // Another tab or device took over this character: do not reconnect until net_resume().
  bool replaced;
  bool idle;
} N;

#include "fastfood_net.inc"

static void set_state(NetState s, const char *text) {
  lock(&N.mu);
  N.state = s;
  if (text != N.status) snprintf(N.status, sizeof N.status, "%s", text);
  unlock(&N.mu);
}

/** The last error is saved on the SD card (last-error.txt) to diagnose real consoles. */
static void report_error(const char *err) {
  static char last[128];
  if (!strcmp(last, err)) return;
  snprintf(last, sizeof last, "%s", err);
  char line[200];
  snprintf(line, sizeof line, "%s -> %s", N.server, err);
  store_save("last-error", line);
}

static void token_key(char *key, int cap) {
  if (N.ep.port == 80 || N.ep.port == 443) snprintf(key, (size_t)cap, "token-%s", N.ep.host);
  else snprintf(key, (size_t)cap, "token-%s-%d", N.ep.host, N.ep.port);
}

// ---------------------------------------------------------------- network thread

static void deliver(const u8 *data, int len, void *ctx) {
  (void)ctx;
  lock(&N.mu);
  if (len > 0 && (data[0] == S_REPLACED || data[0] == S_AFK)) {
    N.replaced = true;
    N.idle = data[0] == S_AFK;
  }
  if (len > 0 && data[0] == S_WELCOME) N.welcomed = true;
  if (len > 0 && data[0] == S_LOCKED) {
    N.blocked = NET_LOCKED;
    N.doorbell = len > 1 && data[1] == 4;
    snprintf(N.status, sizeof N.status, "%s", N.doorbell ? "Ring the doorbell to ask the room owner to enter." : len > 1 && data[1] == 2 ? "Incorrect password. Try again." : "Enter the room password to continue.");
  }
  if (len > 0 && data[0] == S_CAPACITY) {
    Reader r; r_init(&r, data, len); r_u8(&r);
    u8 scope = r_u8(&r); u64 seconds = r_var(&r);
    if (!r.err) {
      if (seconds < 30) seconds = 30;
      if (seconds > 3600) seconds = 3600;
      N.blocked = NET_FULL; N.room_full = scope == 2; N.retry_at = now_ms() + seconds * 1000;
      snprintf(N.status, sizeof N.status, "%s", scope == 2 ? "This room is full. Please choose another room." : "Tweebbo is at capacity. Please try again shortly.");
    }
  }
  if (len > 0 && data[0] == S_MODERATION) {
    Reader r; r_init(&r, data, len); r_u8(&r);
    r_str(&r, N.status, sizeof N.status);
    if (!r.err) N.blocked = NET_DENIED;
  }
  if (!q_push(&N.in, data, len)) {
    q_clear(&N.in); N.want_gen++;
    snprintf(N.status, sizeof N.status, "Refreshing room state…");
  }
  unlock(&N.mu);
}

static bool changed(u32 gen) {
  lock(&N.mu);
  bool c = N.quit || N.want_gen != gen;
  unlock(&N.mu);
  return c;
}

static bool is_replaced(void) {
  lock(&N.mu);
  bool r = N.replaced;
  unlock(&N.mu);
  return r;
}

static bool is_blocked(void) {
  lock(&N.mu);
  bool b = N.blocked != 0;
  unlock(&N.mu);
  return b;
}

static void net_thread(void *arg) {
  (void)arg;
  int failures = 0;
  while (!N.quit) {
    if (is_blocked()) {
      lock(&N.mu); N.state = N.blocked; unlock(&N.mu);
      while (is_blocked() && !N.quit) sleep_ms(50);
      failures = 0;
      continue;
    }
    if (is_replaced()) {
      // Reconnecting now would evict the other session, which would evict us again.
      set_state(NET_REPLACED, N.idle ? "Disconnected after 30 minutes without activity. Choose Play here to return." : "Tweebbo is open on another device or browser.");
      while (is_replaced() && !N.quit) sleep_ms(50);
      failures = 0;
      continue;
    }
    lock(&N.mu);
    u32 gen = N.want_gen;
    char path[96], token[80], arrival[80], password[65];
    N.welcomed = false;
    snprintf(password, sizeof password, "%s", N.password);
    snprintf(path, sizeof path, "/ws/%s", N.slug);
    snprintf(token, sizeof token, "%s", N.token);
    snprintf(arrival, sizeof arrival, "%s", N.arrival);
    unlock(&N.mu);

    set_state(failures ? NET_RETRYING : NET_CONNECTING, failures ? N.status : "Connecting…");
    Ws ws = {0};
    char err[128];
    if (!conn_global_init(err, sizeof err) || !ws_connect(&ws, &N.ep, path, 15000, err, sizeof err)) {
      if (ws.http_status == 404) {
        lock(&N.mu);
        N.missing_room = true;
        N.missing_room_gen = gen;
        unlock(&N.mu);
      }
      report_error(err);
      failures++;
      set_state(NET_RETRYING, err);
      // Exponential backoff with jitter, like the web: 1, 2, 4… up to 15 s.
      int wait = (failures > 4 ? 15000 : 1000 << (failures - 1)) + rnd_range(0, 700);
      for (int t = 0; t < wait && !changed(gen); t += 50) sleep_ms(50);
      continue;
    }
    Writer w;
    w_begin(&w, C_HELLO); w_u16(&w, PROTOCOL_VERSION); w_str(&w, token); w_str(&w, arrival); w_u8(&w, PLATFORM_3DS); w_str(&w, password); w_u8(&w, NATIVE_STATUS | NATIVE_GAMES | NATIVE_CASINO | NATIVE_SNOW | NATIVE_SNOW2 | NATIVE_ROOMMOD); w_u8(&w, DEVICE_3DS); w_u8(&w, FEATURE_ECONOMY | FEATURE_ACTIVITIES | FEATURE_MUSIC | FEATURE_IGLOO);
    bool alive = ws_send(&ws, w.buf, w.len);
    lock(&N.mu);
    // The arrival ticket is only valid once.
    if (!strcmp(N.arrival, arrival)) N.arrival[0] = 0;
    q_clear(&N.out);
    unlock(&N.mu);
    u64 since = now_ms();
    bool welcomed = false;
    while (alive && !changed(gen)) {
      lock(&N.mu);
      Msg *m;
      while (alive && (m = q_pop(&N.out))) {
        unlock(&N.mu);
        alive = ws_send(&ws, m->data, m->len);
        free(m);
        lock(&N.mu);
      }
      unlock(&N.mu);
      if (!alive) break;
      alive = ws_poll(&ws, 4, deliver, NULL);
      lock(&N.mu);
      if (N.welcomed && !welcomed) {
        welcomed = true;
        N.state = NET_ONLINE;
        N.status[0] = 0;
      }
      unlock(&N.mu);
      if (welcomed) failures = 0;
      if (now_ms() - since > RENEW_MS) break;
      if (is_replaced() || is_blocked()) break;
    }
    ws_close(&ws);
    if (is_replaced() || is_blocked()) continue;
    if (!changed(gen) && !(alive && welcomed)) {
      failures++;
      set_state(NET_RETRYING, welcomed ? "Connection lost. Reconnecting…" : "The server closed the connection.");
      int wait = (failures > 4 ? 15000 : 1000 << (failures - 1)) + rnd_range(0, 700);
      for (int t = 0; t < wait && !changed(gen); t += 50) sleep_ms(50);
    }
  }
}

static void http_thread(void *arg) {
  (void)arg;
  while (!N.quit) {
    HttpJob *job = NULL;
    lock(&N.mu);
    for (int i = 0; i < MAX_HTTP && !job; i++) if (N.http[i].queued && !N.http[i].done) job = &N.http[i];
    char token[80];
    snprintf(token, sizeof token, "%s", N.token);
    unlock(&N.mu);
    if (!job) { sleep_ms(30); continue; }
    HttpResult res;
    if (!conn_ready()) {
      // The network thread sets up the sockets; without them the request fails with a clear message.
      memset(&res, 0, sizeof res);
      snprintf(res.err, sizeof res.err, "Not connected to the network yet.");
      lock(&N.mu);
      job->res = res;
      job->done = true;
      unlock(&N.mu);
      continue;
    }
    Endpoint prod;
    endpoint_parse(PROD_SERVER, &prod);
    http_request(job->prod ? &prod : &N.ep, job->method, job->path, job->auth ? token : NULL, job->json, 20000, &res);
    lock(&N.mu);
    job->res = res;
    job->done = true;
    unlock(&N.mu);
  }
}

// ---------------------------------------------------------------- main thread

void sim_deliver(const u8 *data, int len) { world_handle(data, len); }

static void start_offline_room(const char *slug, u64 now) {
  N.offline = true;
  N.state = NET_OFFLINE;
  snprintf(N.slug, sizeof N.slug, "%s", slug && *slug ? slug : "lobby");
  sim_start(N.slug, now);
  Writer w;
  w_begin(&w, C_HELLO); w_u16(&w, PROTOCOL_VERSION); w_str(&w, ""); w_str(&w, ""); w_u8(&w, PLATFORM_3DS);
  net_send(w.buf, w.len);
}

static void start_offline(u64 now) { start_offline_room("lobby", now); }

void net_start(u64 now) {
  if (!N.started) mutex_init(&N.mu);
  N.started = true;
  N.quit = false;
  if (N.session_server[0]) snprintf(N.server, sizeof N.server, "%s", N.session_server);
  else if (!store_load("server", N.server, sizeof N.server) || !N.server[0]) snprintf(N.server, sizeof N.server, DEFAULT_SERVER);
  if (!net_server_allowed(N.server)) snprintf(N.server, sizeof N.server, DEFAULT_SERVER);
#ifdef TWEEBBO_SHOWCASE
  // Proof of concept: every launch tries the hotel of the local network first; offline is
  // offered on screen when it does not answer (a choice that lasts until the app closes).
  static bool launched;
  if (!launched && !strcmp(N.server, "offline")) snprintf(N.server, sizeof N.server, DEFAULT_SERVER);
  launched = true;
#endif
  // Offline only when the player chose it: a network failure never silently switches to the simulation.
  if (!strcmp(N.server, "offline")) { start_offline(now); return; }
  if (!endpoint_parse(N.server, &N.ep)) {
    snprintf(N.server, sizeof N.server, DEFAULT_SERVER);
    endpoint_parse(N.server, &N.ep);
  }
  N.offline = false;
  N.replaced = false;
  N.blocked = 0;
  char key[128];
  token_key(key, sizeof key);
  if (!store_load(key, N.token, sizeof N.token)) N.token[0] = 0;
  N.token_saved = true;
  if (!store_load("room", N.slug, sizeof N.slug) || !N.slug[0]) snprintf(N.slug, sizeof N.slug, DEFAULT_ROOM);
  N.arrival[0] = 0;
  N.password[0] = 0;
  N.missing_room = false;
  N.want_gen++;
  N.state = NET_CONNECTING;
  snprintf(N.status, sizeof N.status, "Connecting…");
  N.thread = spawn(net_thread);
  N.http_thread = spawn(http_thread);
}

void net_stop(void) {
  if (!N.started || N.offline) return;
  lock(&N.mu);
  N.quit = true;
  unlock(&N.mu);
  join(N.thread);
  join(N.http_thread);
  join(N.food_thread);
  N.food_thread=0;N.food_enabled=false;
  q_clear(&N.food_in);q_clear(&N.food_out);
  N.thread = N.http_thread = 0;
  q_clear(&N.in);
  q_clear(&N.out);
  for (int i = 0; i < MAX_HTTP; i++) { free(N.http[i].json); free(N.http[i].res.body); memset(&N.http[i], 0, sizeof N.http[i]); }
}

bool net_server_allowed(const char *url) {
#if defined(TWEEBBO_DEBUG) || defined(TWEEBBO_SHOWCASE)
  if (!strcmp(url, "offline")) return true;
  Endpoint ep; unsigned a, b, c, d; char extra;
  if (!endpoint_parse(url, &ep) || ep.tls || sscanf(ep.host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return false;
  return a == 10 || a == 127 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254);
#else
  // Public build: only tweebbo.lol, or offline. A LAN address saved by a showcase or debug
  // build would otherwise keep the console away from the public hotel for good.
  return !strcmp(url, "offline") || !strcmp(url, DEFAULT_SERVER);
#endif
}

void net_set_server(const char *url) {
  if (!net_server_allowed(url)) {
#if defined(TWEEBBO_DEBUG) || defined(TWEEBBO_SHOWCASE)
    on_notice("This build connects to private LAN addresses only.");
#else
    on_notice("This build connects to tweebbo.lol only. Use Offline to play without a hotel.");
#endif
    return;
  }
  net_stop();
  snprintf(N.session_server, sizeof N.session_server, "%s", url);
  if (strcmp(url, "offline")) store_save("server", url);
  free(W.pets_json); free(W.pet_inv_json); free(W.owners_json); free(W.rights_json);
  memset(&W, 0, sizeof W);
  net_start(now_ms());
}

void net_send(const u8 *data, int len) {
  if (N.offline) { sim_receive(data, len); return; }
  lock(&N.mu);
  if (N.state == NET_ONLINE && !q_push(&N.out, data, len)) {
    q_clear(&N.out); N.want_gen++;
  }
  unlock(&N.mu);
}

void net_update(u64 now) {
  if (N.offline) { sim_update(now); return; }
  const char *reception = tw_reception_slug();
  lock(&N.mu);
  bool recover = N.missing_room && N.missing_room_gen == N.want_gen && strcmp(N.slug, reception);
  N.missing_room = false;
  unlock(&N.mu);
  if (recover) {
    // Navigate on the main thread; a late response must not replace a newer destination.
    net_goto(reception, "");
    on_notice("This room does not exist.");
  }
  for (int budget = 0; budget < 256; budget++) {
    lock(&N.mu);
    Msg *m = q_pop(&N.in);
    unlock(&N.mu);
    if (!m) break;
    world_handle(m->data, m->len);
    free(m);
  }
  // The server returns the guest token in WELCOME: it is saved per server.
  if (W.token[0]) {
    lock(&N.mu);
    bool fresh = strcmp(W.token, N.token) != 0;
    if (fresh) snprintf(N.token, sizeof N.token, "%s", W.token);
    unlock(&N.mu);
    if (fresh || !N.token_saved) {
      char key[128];
      token_key(key, sizeof key);
      store_save(key, W.token);
      N.token_saved = true;
    }
  }
}

bool net_online(void) { return !N.offline && net_state() == NET_ONLINE; }

NetState net_state(void) {
  if (N.offline) return NET_OFFLINE;
  lock(&N.mu);
  NetState s = N.state;
  unlock(&N.mu);
  return s;
}

const char *net_status(void) {
  static char copy[128];
  if (!N.started) return "";
  lock(&N.mu);
  snprintf(copy, sizeof copy, "%s", N.status);
  unlock(&N.mu);
  return copy;
}

const char *net_server(void) { return N.server; }
const char *net_room(void) { return N.slug; }
const char *net_token(void) { return N.token; }

void net_goto(const char *slug, const char *arrival) {
  if (N.offline) {
    // Offline practice: the simulation hosts every public room, so changing rooms simply
    // restarts it and rejoins with the saved local profile.
    free(W.pets_json); free(W.pet_inv_json); free(W.owners_json); free(W.rights_json);
    memset(&W, 0, sizeof W);
    start_offline_room(slug, now_ms());
    return;
  }
  lock(&N.mu);
  N.replaced = false;
  N.blocked = 0;
  N.password[0] = 0;
  snprintf(N.slug, sizeof N.slug, "%s", slug);
  snprintf(N.arrival, sizeof N.arrival, "%s", arrival ? arrival : "");
  N.want_gen++;
  N.state = NET_CONNECTING;
  snprintf(N.status, sizeof N.status, "Connecting…");
  q_clear(&N.in);
  q_clear(&N.out);
  unlock(&N.mu);
  W.in_room = false;
  store_save("room", slug);
}

void net_use_token(const char *token) {
  if (N.offline) return;
  lock(&N.mu);
  N.replaced = false;
  N.blocked = 0;
  snprintf(N.token, sizeof N.token, "%s", token);
  N.want_gen++;
  q_clear(&N.in);
  unlock(&N.mu);
  char key[128];
  token_key(key, sizeof key);
  store_save(key, token);
  memset(W.token, 0, sizeof W.token);
  W.in_room = false;
}

int net_http(const char *method, const char *path, const char *json, bool auth) {
  if (N.offline) return 0;
  int id = 0;
  lock(&N.mu);
  for (int i = 0; i < MAX_HTTP; i++) {
    HttpJob *j = &N.http[i];
    if (j->queued) continue;
    memset(j, 0, sizeof *j);
    j->id = id = ++N.next_http;
    snprintf(j->method, sizeof j->method, "%s", method);
    snprintf(j->path, sizeof j->path, "%s", path);
    j->json = json ? strdup(json) : NULL;
    j->auth = auth;
    j->queued = true;
    break;
  }
  unlock(&N.mu);
  return id;
}

int net_check_prod(void) {
  int id = net_http("GET", "/healthz", NULL, false);
  lock(&N.mu);
  for (int i = 0; i < MAX_HTTP; i++) if (N.http[i].id == id) N.http[i].prod = true;
  unlock(&N.mu);
  return id;
}

bool net_http_result(int id, HttpResult *out) {
  bool done = false;
  lock(&N.mu);
  for (int i = 0; i < MAX_HTTP; i++) {
    HttpJob *j = &N.http[i];
    if (!j->queued || j->id != id || !j->done) continue;
    *out = j->res;
    free(j->json);
    memset(j, 0, sizeof *j);
    done = true;
  }
  unlock(&N.mu);
  return done;
}

// ---------------------------------------------------------------- messages

#define SEND(...) do { Writer w; __VA_ARGS__; net_send(w.buf, w.len); } while (0)

void send_walk(int x, int y) { SEND(w_begin(&w, C_WALK); w_u8(&w, (u32)x); w_u8(&w, (u32)y)); }
void send_chat(const char *text) { SEND(w_begin(&w, C_CHAT); w_str(&w, text)); }
void send_gesture(int kind) { SEND(w_begin(&w, C_GESTURE); w_u8(&w, (u32)kind)); }
void send_use(u32 item) { SEND(w_begin(&w, C_USE); w_var(&w, item)); }
void send_use_op(u32 item, u8 op) { SEND(w_begin(&w, C_USE); w_var(&w, item); w_u8(&w, op)); }
void send_look(u64 look) { SEND(w_begin(&w, C_SET_LOOK); w_var(&w, look)); }
void send_name(const char *name) { SEND(w_begin(&w, C_SET_NAME); w_str(&w, name)); }
void send_buy(u16 offer) { SEND(w_begin(&w, C_BUY); w_u16(&w, offer)); }
void send_place(u32 id, int x, int y, int rot, int z) {
  SEND(w_begin(&w, C_PLACE); w_var(&w, id); w_u8(&w, (u32)x); w_u8(&w, (u32)y); w_u8(&w, (u32)rot); w_u16(&w, (u32)z));
}
void send_move(u32 id, int x, int y, int rot, int z) {
  SEND(w_begin(&w, C_MOVE); w_var(&w, id); w_u8(&w, (u32)x); w_u8(&w, (u32)y); w_u8(&w, (u32)rot); w_u16(&w, (u32)z));
}
void send_pickup(u32 id) { SEND(w_begin(&w, C_PICKUP); w_var(&w, id)); }
void send_rotate(u32 id) { SEND(w_begin(&w, C_ROTATE); w_var(&w, id)); }
void send_redeem(u32 id) { SEND(w_begin(&w, C_REDEEM); w_var(&w, id)); }
void send_drink(u32 item, int drink) { SEND(w_begin(&w, C_DRINK); w_var(&w, item); w_u8(&w, (u32)drink)); }
void send_pet(const char *json) { SEND(w_begin(&w, C_PET); w_str(&w, json)); }

void net_resume(void) {
  lock(&N.mu);
  if (N.blocked == NET_FULL && now_ms() < N.retry_at) { unlock(&N.mu); return; }
  N.replaced = false;
  N.blocked = 0;
  N.want_gen++;
  unlock(&N.mu);
}
void net_activity(void) {
  static u64 last;
  u64 now=now_ms();
  if(net_online() && (!last || now-last>=15000)){
    const u8 active=C_ACTIVE;
    net_send(&active,1);last=now;
  }
}

void net_unlock(const char *password) {
  lock(&N.mu);
  snprintf(N.password, sizeof N.password, "%s", password);
  N.blocked = 0; N.want_gen++; N.state = NET_CONNECTING;
  unlock(&N.mu);
}

int net_retry_seconds(void) {
  lock(&N.mu);
  u64 now = now_ms();
  int seconds = N.retry_at > now ? (int)((N.retry_at - now + 999) / 1000) : 0;
  unlock(&N.mu);
  return seconds;
}

bool net_room_full(void) {
  lock(&N.mu);
  bool full = N.blocked == NET_FULL && N.room_full;
  unlock(&N.mu);
  return full;
}

void send_decorate(int kind, const char *id) { SEND(w_begin(&w, C_DECORATE); w_u8(&w, kind); w_str(&w, id)); }

void send_game(int op, u32 a, int b, int c, int d) { SEND(w_begin(&w, C_GAME); w_u8(&w, op); w_var(&w, a); w_u8(&w, b); w_u8(&w, c); w_u8(&w, d)); }
void send_room_mod(int op, u32 target, int length) { SEND(w_begin(&w, C_ROOM_MOD); w_u8(&w, op); w_var(&w, target); if (length) w_u8(&w, length)); }
void send_trade(const char *json) { SEND(w_begin(&w, C_TRADE); w_str(&w, json)); }
void send_activity(const char *json) { SEND(w_begin(&w, C_ACTIVITY); w_str(&w, json)); }
void send_music(const char *json) { SEND(w_begin(&w, C_MUSIC); w_str(&w, json)); }
void send_casino(int op, int a, int b, int c) { SEND(w_begin(&w, C_CASINO); w_u8(&w, op); w_u8(&w, a); w_u8(&w, b); w_u8(&w, c)); }

bool net_doorbell(void) { lock(&N.mu); bool value=N.blocked==NET_LOCKED&&N.doorbell; unlock(&N.mu); return value; }
