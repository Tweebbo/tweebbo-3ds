#include "download.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
u64 now_ms(void) { struct timeval t; gettimeofday(&t, NULL); return (u64)t.tv_sec * 1000 + t.tv_usec / 1000; }
int rnd_range(int a, int b) { (void)b; return a; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
bool net_cancelled(void) { return false; }
typedef struct { long offset, stop; FILE *file; } Output;
static bool receive(const u8 *data, int len, void *ctx) {
  Output *out = ctx;
  if (out->stop && out->offset + len > out->stop) return false;
  for (int i = 0; i < len; i++) assert(data[i] == (u8)((out->offset + i) * 31 + 7));
  if (out->file && fwrite(data, 1, len, out->file) != (size_t)len) return false;
  out->offset += len;
  return true;
}
int main(int argc, char **argv) {
  assert(argc >= 5);
  char error[160]; long total = -1;
  Output out = {.offset = atol(argv[3]), .stop = atol(argv[4])};
  if (argc > 5) { out.file = fopen(argv[5], "wb"); assert(out.file); setvbuf(out.file, NULL, _IOFBF, 128 * 1024); }
  assert(conn_global_init(error, sizeof error));
  u64 start = now_ms();
  int status = http_download_optimized_from(argv[1], out.offset, atoi(argv[2]), receive, &out, &total, error, sizeof error);
  if (!status) fprintf(stderr, "Download error: %s\n", error);
  if (out.file) assert(!fclose(out.file));
  printf("{\"status\":%d,\"total\":%ld,\"offset\":%ld,\"ms\":%llu}\n", status, total, out.offset, (unsigned long long)(now_ms()-start));
  conn_global_exit();
  return 0;
}
