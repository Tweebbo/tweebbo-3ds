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
void on_transfer(const char *slug, const char *arrival) { transfers++; net_goto(slug, arrival); }
void on_alert(const char *json) { printf("  alerta: %s\n", json); }
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
  snprintf(key, sizeof key, "token-%s", ep.host);
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
  net_stop();
  printf("OK\n");
  return 0;
}
