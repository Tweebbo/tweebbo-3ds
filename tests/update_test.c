// Fetches latest.json and the CIA from the GitHub release with the console's network code
// (redirects, embedded root certificates) and checks the SHA-256, like the in-game updater.
// Runs in ubuntu:24.04: make -C tests update
#include "../source/ws.h"
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
u64 now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return (u64)tv.tv_sec * 1000 + (u64)tv.tv_usec / 1000; }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + rand() % (hi - lo); }

typedef struct { char *buf; int len, cap; mbedtls_sha256_context sha; long bytes; } Sink;
static bool to_mem(const u8 *d, int n, void *ctx) {
  Sink *s = ctx;
  if (s->len + n + 1 > s->cap) return false;
  memcpy(s->buf + s->len, d, (size_t)n); s->len += n; s->buf[s->len] = 0;
  return true;
}
static bool to_hash(const u8 *d, int n, void *ctx) { Sink *s = ctx; mbedtls_sha256_update_ret(&s->sha, d, (size_t)n); s->bytes += n; return true; }
static void field(const char *json, const char *key, char *out, int cap) {
  char pat[64]; snprintf(pat, sizeof pat, "\"%s\": \"", key);
  const char *p = strstr(json, pat); out[0] = 0;
  if (!p) return;
  p += strlen(pat);
  int n = 0; while (p[n] && p[n] != '"' && n < cap - 1) { out[n] = p[n]; n++; } out[n] = 0;
}

int main(int argc, char **argv) {
  const char *manifest = argc > 1 ? argv[1] : "https://github.com/Tweebbo/tweebbo-3ds/releases/latest/download/latest.json";
  char err[160];
  if (!conn_global_init(err, sizeof err)) return 1;
  Sink m = {malloc(16384), 0, 16384};
  long total;
  int st = http_download(manifest, to_mem, &m, &total, err, sizeof err);
  printf("manifest %d %s\n", st, err);
  if (st != 200) return 1;
  char version[32], cia[400], sha[80];
  field(m.buf, "version", version, sizeof version); field(m.buf, "cia", cia, sizeof cia); field(m.buf, "sha256", sha, sizeof sha);
  printf("version %s\ncia %s\n", version, cia);
  Sink c = {0};
  mbedtls_sha256_init(&c.sha); mbedtls_sha256_starts_ret(&c.sha, 0);
  u64 t0 = now_ms();
  st = http_download(cia, to_hash, &c, &total, err, sizeof err);
  u8 digest[32]; char hex[65];
  mbedtls_sha256_finish_ret(&c.sha, digest);
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  printf("cia %d, %ld bytes in %llu ms, sha256 %s\n", st, c.bytes, (unsigned long long)(now_ms() - t0), strcmp(hex, sha) ? "MISMATCH" : "ok");
  return st == 200 && !strcmp(hex, sha) ? 0 : 1;
}
