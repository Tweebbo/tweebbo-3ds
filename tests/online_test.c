// PC test of the 3DS online client (net.c + ws.c + conn.c) against a
// real server. Only for local or test servers: it creates a guest.
//   make -C tests online SERVER=ws://127.0.0.1:8092
#include "../source/net.h"
#include "../source/proto.h"
#include "../source/world.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

u64 now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return (u64)tv.tv_sec * 1000 + (u64)tv.tv_usec / 1000; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + rand() % (hi - lo); }

static int rooms, chats, transfers;
static char last_chat[256], last_notice[256];
void on_room(void) { rooms++; }
void on_chat(u32 eid, const char *text) { chats++; snprintf(last_chat, sizeof last_chat, "%u:%s", eid, text); }
void on_notice(const char *text) { snprintf(last_notice, sizeof last_notice, "%s", text); printf("  notice: %s\n", text); }
void on_kicked(const char *text) { (void)text; }
void on_transfer(const char *slug, const char *arrival) { transfers++; net_goto(slug, arrival); }
void on_alert(const char *json) { printf("  alert: %s\n", json); }
void on_casino(const char *json) { (void)json; }
void on_snow(const u8 *data,int len) { (void)data;(void)len; }
void on_social_changed(void) {}
void on_name_changed(u32 eid) { (void)eid; }
void on_item(u32 id) { (void)id; }
// The offline simulation is not used here.
void sim_start(const char *k, u64 n) { (void)k; (void)n; }
void sim_receive(const u8 *d, int l) { (void)d; (void)l; }
void sim_update(u64 n) { (void)n; }

static const char *server;
static char store_dir[] = "/tmp/tweebbo-3ds-online-XXXXXX";
bool store_load(const char *key, char *out, int cap) {
  if (!strcmp(key, "server")) { snprintf(out, (size_t)cap, "%s", server); return true; }
  char path[256];
  snprintf(path, sizeof path, "%s/%s", store_dir, key);
  FILE *f = fopen(path, "r");
  if (!f) return false;
  int n = (int)fread(out, 1, (size_t)cap - 1, f);
  out[n > 0 ? n : 0] = 0;
  fclose(f);
  return true;
}
void store_save(const char *key, const char *value) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s", store_dir, key);
  FILE *f = fopen(path, "w");
  if (f) { fputs(value, f); fclose(f); }
}

static bool pump_until(bool (*cond)(void), int ms) {
  for (u64 end = now_ms() + (u64)ms; now_ms() < end;) {
    net_update(now_ms());
    if (cond()) return true;
    usleep(20000);
  }
  return false;
}
static bool in_room(void) { return W.in_room && world_ent(W.me); }
static bool got_chat(void) { return strstr(last_chat, "hello from the 3DS") != NULL; }
static bool paused(void) { return net_state() == NET_REPLACED; }
static bool back_online(void) { return net_state() == NET_ONLINE && in_room(); }
static void ignore(const u8 *data, int len, void *ctx) { (void)data; (void)len; (void)ctx; }

static u32 browser_id, browser_invite;
static Duel browser_game;
static void browser_receive(const u8 *data, int len, void *ctx) {
  (void)ctx;
  Reader r; r_init(&r, data, len);
  int type = r_u8(&r);
  if (type == S_WELCOME) browser_id = (u32)r_var(&r);
  if (type != S_GAME) return;
  int op = r_u8(&r);
  if (op == 1) browser_invite = (u32)r_var(&r);
  if (op != 2) return;
  Duel keep = W.duel; u32 revision = W.game_rev;
  world_handle(data, len); browser_game = W.duel;
  W.duel = keep; W.game_rev = revision;
}

static void browser_pump(Ws *ws, int ms) {
  for (u64 end = now_ms() + ms; now_ms() < end;) {
    assert(ws_poll(ws, 10, browser_receive, NULL)); net_update(now_ms());
  }
}

static void crossplay_games(Endpoint *ep) {
  Ws peer; Writer w; char err[128];
  assert(ws_connect(&peer, ep, "/ws/lobby", 15000, err, sizeof err));
  w_begin(&w, C_HELLO); w_u16(&w, PROTOCOL_VERSION); w_str(&w, ""); w_str(&w, ""); w_u8(&w, 0); w_str(&w, ""); w_u8(&w, 2); /* browser build marker */
  assert(ws_send(&peer, w.buf, w.len)); browser_pump(&peer, 1500);
  assert(browser_id && world_ent(browser_id));
  assert(!(world_ent(browser_id)->flags & FLAG_3DS));
  for (int kind = 1; kind <= 3; kind++) {
    browser_invite = 0;
    send_game(1, browser_id, kind, 0, 0); browser_pump(&peer, 1000);
    assert(browser_invite == W.me);
    w_begin(&w, C_GAME); w_u8(&w, 2); w_var(&w, W.me); w_u8(&w, 1);
    assert(ws_send(&peer, w.buf, w.len)); browser_pump(&peer, 1000);
    assert(W.duel.id && W.duel.kind == kind && W.duel.move_count > 0);
    assert(!memcmp(W.duel.board, browser_game.board, 64));
    assert(W.duel.players[W.duel.turn] == W.me);
    send_game(3, W.duel.id, W.duel.moves[0][0], W.duel.moves[0][1], 5); browser_pump(&peer, 1000);
    assert(W.duel.turn == 1 && !memcmp(W.duel.board, browser_game.board, 64));
    w_begin(&w, C_GAME); w_u8(&w, 3); w_var(&w, browser_game.id);
    w_u8(&w, browser_game.moves[0][0]); w_u8(&w, browser_game.moves[0][1]); w_u8(&w, 5);
    assert(ws_send(&peer, w.buf, w.len)); browser_pump(&peer, 1000);
    assert(W.duel.turn == 0 && !memcmp(W.duel.board, browser_game.board, 64));
    send_game(4, W.duel.id, 0, 0, 0); browser_pump(&peer, 1000);
    assert(W.duel.result == 2 && browser_game.result == 2);
  }
  ws_close(&peer);
  puts("native/browser crossplay: Tic-tac-toe, Connect Four and Chess moves and resignation passed");
}

int main(int argc, char **argv) {
  server = argc > 1 ? argv[1] : "ws://127.0.0.1:8092";
  if (!mkdtemp(store_dir)) return 1;
  srand((unsigned)now_ms());
  net_start(now_ms());
  assert(pump_until(in_room, 20000) && "could not enter the lobby");
  Ent *me = world_ent(W.me);
  printf("online: room %s \"%s\" (%s), me=%s eid=%u guest=%u credits=%u inventory=%d\n", W.slug, W.title, W.model,
         W.name, W.me, W.guest_id, W.credits, W.inv_n);
  assert(me->flags & FLAG_3DS);
  printf("3DS flag on my entity: yes\n");

  char token[80];
  char key[160];
  Endpoint ep;
  endpoint_parse(server, &ep);
  if (ep.port == 80 || ep.port == 443) snprintf(key, sizeof key, "token-%s", ep.host);
  else snprintf(key, sizeof key, "token-%s-%d", ep.host, ep.port);
  assert(store_load(key, token, sizeof token) && token[0]);
  printf("token saved per server: yes (%zu characters)\n", strlen(token));

  send_chat("hello from the 3DS");
  assert(pump_until(got_chat, 5000));
  printf("chat round trip: %s\n", last_chat);

  // HTTP API: room list with authentication.
  int job = net_http("GET", "/api/rooms?scope=all", NULL, true);
  HttpResult res;
  u64 end = now_ms() + 10000;
  while (!net_http_result(job, &res) && now_ms() < end) usleep(20000);
  printf("  response %d: %.120s %s\n", res.status, res.body ? res.body : "", res.err);
  assert(res.status == 200 && res.body && (res.body[0] == '[' || res.body[0] == '{'));
  printf("GET /api/rooms: %d, %d bytes\n", res.status, res.len);
  free(res.body);

  // Change room and come back: the identity is kept.
  u32 guest = W.guest_id;
  net_goto("park", "");
  rooms = 0;
  assert(pump_until(in_room, 20000));
  printf("park: %s (%s) %dx%d, same character: %s\n", W.slug, W.model, W.w, W.h, W.guest_id == guest ? "yes" : "no");
  assert(W.guest_id == guest);
  net_goto("lobby", "");
  assert(pump_until(in_room, 20000));
  assert(W.guest_id == guest);

  crossplay_games(&ep);

  // Another client with the same character takes over (a browser tab after "Save /
  // restore character"). The console must pause instead of evicting it back forever.
  Ws other;
  char err[128];
  assert(ws_connect(&other, &ep, "/ws/lobby", 15000, err, sizeof err));
  Writer w;
  w_begin(&w, C_HELLO); w_u16(&w, PROTOCOL_VERSION); w_str(&w, token); w_str(&w, ""); w_u8(&w, 0); w_str(&w, ""); w_u8(&w, 2); /* browser build marker */
  assert(ws_send(&other, w.buf, w.len));
  assert(pump_until(paused, 10000) && "the console was not told it was replaced");
  for (u64 stop = now_ms() + 4000; now_ms() < stop;) {
    net_update(now_ms());
    assert(ws_poll(&other, 20, ignore, NULL) && "the console evicted the other client");
    assert(net_state() == NET_REPLACED);
  }
  printf("takeover: console paused, other client kept the character\n");
  // "Play here" takes the character back; now the other client is released.
  net_resume();
  assert(pump_until(back_online, 20000));
  assert(W.guest_id == guest);
  bool released = false;
  for (u64 stop = now_ms() + 10000; now_ms() < stop && !released;) {
    net_update(now_ms());
    released = !ws_poll(&other, 20, ignore, NULL);
  }
  ws_close(&other);
  assert(released);
  printf("play here: console back online, other client released\n");
  net_stop();
  printf("OK\n");
  return 0;
}
