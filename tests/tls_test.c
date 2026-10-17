// Checks TLS (mbedTLS 2.28, embedded root certificates) against a real server with
// a harmless request: GET /healthz. Runs in ubuntu:24.04 (make tls).
#include "../source/ws.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
u64 now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return (u64)tv.tv_sec * 1000 + (u64)tv.tv_usec / 1000; }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + rand() % (hi - lo); }
void dbg_log(const char *fmt, ...) { (void)fmt; }
int main(int argc, char **argv) {
  Endpoint ep;
  if (!endpoint_parse(argc > 1 ? argv[1] : "https://tweebbo.lol", &ep) || !conn_global_init((char[128]){0}, 128)) return 1;
  HttpResult r;
  u64 t0 = now_ms();
  http_request(&ep, "GET", "/healthz", NULL, NULL, 20000, &r);
  printf("%s:%d tls=%d -> %d \"%s\" %s (%llu ms)\n", ep.host, ep.port, ep.tls, r.status, r.body ? r.body : "", r.err,
         (unsigned long long)(now_ms() - t0));
  return r.status == 200 && r.body && !strcmp(r.body, "ok") ? 0 : 1;
}
