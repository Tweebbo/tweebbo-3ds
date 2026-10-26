#include "net.h"
#include "world.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
static const char *server;
static bool received_notice;
u64 now_ms(void) { struct timeval t; gettimeofday(&t, NULL); return (u64)t.tv_sec * 1000 + t.tv_usec / 1000; }
int rnd_range(int a, int b) { (void)b; return a; }
void dbg_log(const char *s, ...) { (void)s; }
void on_room(void) {}
void on_chat(u32 a, const char *b) { (void)a; (void)b; }
void on_notice(const char *s) { (void)s; received_notice=true; }
void on_kicked(const char *text) { (void)text; }
void on_transfer(const char *a, const char *b) { (void)a; (void)b; }
void on_alert(const char *s) { (void)s; }
void on_casino(const char *s) { (void)s; }
void on_trade(const char *s) { (void)s; }
void on_activity(const char *s) { (void)s; }
void on_music(const char *s) { (void)s; }
void on_snow(const u8 *data,int len) { (void)data;(void)len; }
void on_social_changed(void) {}
void on_name_changed(u32 i) { (void)i; }
void on_item(u32 i) { (void)i; }
void sim_start(const char *s, u64 t) { (void)s; (void)t; }
void sim_receive(const u8 *d, int l) { (void)d; (void)l; }
void sim_update(u64 t) { (void)t; }
const char *tw_reception_slug(void) { return "lobby-es"; }
bool store_load(const char *key, char *out, int cap) {
  if (strcmp(key, "server")) return false;
  snprintf(out, cap, "%s", server); return true;
}
void store_save(const char *key, const char *value) { (void)key; (void)value; }
int main(int argc, char **argv) {
  assert(argc == 3); server = argv[1];
  int mode = atoi(argv[2]); net_start(now_ms());
  NetState want = mode == 1 ? NET_LOCKED : mode == 2 ? NET_FULL : mode == 3 ? NET_DENIED : NET_RETRYING;
  u64 end = now_ms() + 4000;
  while (now_ms() < end && (net_state() != want || !received_notice)) {
    net_update(now_ms()); assert(net_state() != NET_ONLINE); usleep(1000);
  }
  assert(net_state() == want);
  assert(received_notice);
  if (mode == 1) {
    net_unlock("correct"); end = now_ms() + 4000;
    while (now_ms() < end && net_state() != NET_ONLINE) { net_update(now_ms()); usleep(1000); }
    assert(net_state() == NET_ONLINE);
  }
  if (mode == 2) { assert(!net_room_full()); assert(net_retry_seconds() >= 29); net_resume(); assert(net_state() == NET_FULL); }
  net_stop(); conn_global_exit();
  puts("transport state passed");
  return 0;
}
