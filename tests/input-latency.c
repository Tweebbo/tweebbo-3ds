#define main transport_state_main
#define on_chat transport_on_chat
#include "transport_test.c"
#undef main
#undef on_chat

static unsigned echoes;
void on_chat(u32 id, const char *text) { (void)id; (void)text; echoes++; }
static int compare(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
int main(int argc, char **argv) {
  assert(argc == 2); server = argv[1]; net_start(now_ms());
  u64 deadline = now_ms() + 5000;
  while(net_state() != NET_ONLINE && now_ms() < deadline) { net_update(now_ms()); usleep(1000); }
  assert(net_state() == NET_ONLINE);
  int samples[120];
  for(unsigned i=0;i<120;i++) {
    usleep((useconds_t)(7+i%19)*1000);
    unsigned before=echoes; u64 start=now_ms();
    send_chat("Input probe");
    while(echoes==before && now_ms()-start<1000) { net_update(now_ms()); usleep(1000); }
    assert(echoes>before); samples[i]=(int)(now_ms()-start);
  }
  qsort(samples,120,sizeof samples[0],compare);
  printf("{\"samples\":120,\"p50Ms\":%d,\"p95Ms\":%d,\"maxMs\":%d}\n",samples[60],samples[114],samples[119]);
  net_stop(); conn_global_exit();
}
