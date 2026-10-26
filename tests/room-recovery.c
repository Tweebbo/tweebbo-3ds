#define main transport_state_main
#define store_load transport_store_load
#define store_save transport_store_save
#include "transport_test.c"
#undef main
#undef store_load
#undef store_save

static char saved_room[64] = "deleted-room";
bool store_load(const char *key, char *out, int cap) {
  const char *value = !strcmp(key, "server") ? server : !strcmp(key, "room") ? saved_room :
                      !strncmp(key, "token-", 6) ? "fixture-identity" : NULL;
  if (!value) return false;
  snprintf(out, cap, "%s", value);
  return true;
}
void store_save(const char *key, const char *value) {
  if (!strcmp(key, "room")) snprintf(saved_room, sizeof saved_room, "%s", value);
  if (!strncmp(key, "token-", 6)) assert(!strcmp(value, "fixture-identity"));
}
int main(int argc, char **argv) {
  assert(argc == 3);
  server = argv[1];
  const char *mode = argv[2];
  net_start(now_ms());
  if (!strcmp(mode, "late")) {
    u64 end = now_ms() + 3000;
    while (net_state() != NET_RETRYING && now_ms() < end) usleep(1000);
    assert(net_state() == NET_RETRYING);
    net_goto("park", "");
  }
  bool admitted = !strcmp(mode, "recover") || !strcmp(mode, "late");
  u64 end = now_ms() + (admitted ? 3000 : 600);
  while (now_ms() < end) {
    net_update(now_ms());
    if (admitted && net_online()) break;
    usleep(1000);
  }
  net_update(now_ms());
  assert(net_online() == admitted);
  const char *expected = !strcmp(mode, "late") ? "park" :
                         (!strcmp(mode, "recover") || !strcmp(mode, "missing-reception")) ? "lobby-es" : "deleted-room";
  assert(!strcmp(net_room(), expected));
  assert(!strcmp(saved_room, expected));
  assert(!strcmp(net_token(), "fixture-identity"));
  net_stop();
  conn_global_exit();
  puts("Room recovery passed.");
}
