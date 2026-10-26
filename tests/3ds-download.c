/* Standalone LAN-only transport probe. It never installs a title or opens character data. */
#include <3ds.h>
#include <mbedtls/sha256.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "download.h"
#ifndef PROBE_HOST
#error Build this probe with the isolated LAN fixture host.
#endif
#ifndef PROBE_SHA
#error Build this probe with the fixture SHA-256.
#endif
static volatile bool finished;
static FILE *log_file;
u64 now_ms(void) { return osGetTime(); }
int rnd_range(int a, int b) { (void)b; return a; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
typedef struct { mbedtls_sha256_context sha; long bytes; } Hash;
static bool receive(const u8 *data, int length, void *ctx) {
  Hash *h = ctx;
  mbedtls_sha256_update_ret(&h->sha, data, length);
  h->bytes += length;
  return true;
}
static void run(void *unused) {
  (void)unused;
  char error[160], url[160];
  bool passed = conn_global_init(error, sizeof error);
  for (int secure = 0; secure < 2 && passed; secure++) {
    snprintf(url, sizeof url, "%s://" PROBE_HOST ":%d/fixture.bin", secure ? "https" : "http", secure ? 18998 : 18997);
    for (int test = 0; test < 4; test++) {
      int connections = test == 0 ? 1 : test == 3 ? 4 : 2;
      long from = test == 2 ? 1048576 : 0, total = 0;
      Hash h = {0};
      mbedtls_sha256_init(&h.sha); mbedtls_sha256_starts_ret(&h.sha, 0);
      u8 prefix[1024]; for (int i = 0; i < 1024; i++) prefix[i] = (u8)(i*31+7);
      while (h.bytes < from) receive(prefix, sizeof prefix, &h);
      u64 start = now_ms();
      int status = http_download_optimized_from(url, from, connections, receive, &h, &total, error, sizeof error);
      u8 digest[32]; char hex[65]; mbedtls_sha256_finish_ret(&h.sha, digest); mbedtls_sha256_free(&h.sha);
      for (int i = 0; i < 32; i++) snprintf(hex+i*2, 3, "%02x", digest[i]);
      bool ok = (status == 200 || status == 206) && h.bytes == total && !strcmp(hex, PROBE_SHA);
      unsigned long elapsed = (unsigned long)(now_ms()-start);
      unsigned long heap = (unsigned long)mallinfo().uordblks;
      printf("%s %d sockets: %s %lu ms\n", secure ? "TLS" : "HTTP", connections, ok ? "PASS" : "FAIL", elapsed);
      fprintf(log_file, "{\"tls\":%s,\"connections\":%d,\"from\":%ld,\"status\":%d,\"bytes\":%ld,\"ms\":%lu,\"heapAfter\":%lu,\"ok\":%s}\n",
              secure ? "true" : "false", connections, from, status, h.bytes, elapsed, heap, ok ? "true" : "false");
      fflush(log_file);
      if (!ok) { passed = false; break; }
    }
  }
  fprintf(log_file, "%s\n", passed ? "PASS" : "FAIL"); fclose(log_file);
  conn_global_exit();
  printf("%s. START exits.\n", passed ? "PASS" : "FAIL");
  finished = true;
}
int main(void) {
  gfxInitDefault(); consoleInit(GFX_BOTTOM, NULL);
  mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/Tweebbo-download-test", 0777);
  log_file = fopen("sdmc:/3ds/Tweebbo-download-test/results.txt", "w");
  if (!log_file) { gfxExit(); return 1; }
  printf("Tweebbo LAN download probe\n");
  Thread worker = threadCreate(run, NULL, 64*1024, 0x31, -2, false);
  if (!worker) { fclose(log_file); gfxExit(); return 1; }
  while (aptMainLoop()) {
    hidScanInput();
    if (finished && (hidKeysDown() & KEY_START)) break;
    gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
  }
  threadJoin(worker, U64_MAX); threadFree(worker); gfxExit();
  return 0;
}
