#include "download.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef __3DS__
#include <3ds.h>
static void pause_io(void) { svcSleepThread(2000000); }
#else
#include <unistd.h>
static void pause_io(void) { usleep(2000); }
#endif

#define PIECE (256 * 1024)
#define CONNECTIONS 4

typedef struct {
  Conn *conn;
  u8 *data;
  long first, last;
  int used;
  bool active;
} Piece;
typedef struct {
  Endpoint ep;
  char url[2048], path[2048], request[3072], head[16385];
  char validator[192], validator_name[24];
  Piece pieces[CONNECTIONS];
} Download;
typedef struct {
  int status;
  long size;
  char location[2048], etag[192], modified[192];
  bool keepalive;
} Reply;

static bool field(const char *head, const char *name, char *out, size_t cap) {
  out[0] = 0;
  size_t n = strlen(name);
  for (const char *p = strstr(head, "\r\n"); p; p = strstr(p, "\r\n")) {
    p += 2;
    if (strncasecmp(p, name, n) || p[n] != ':') continue;
    p += n + 1;
    while (*p == ' ' || *p == '\t') p++;
    size_t len = strcspn(p, "\r\n");
    while (len && (p[len-1] == ' ' || p[len-1] == '\t')) len--;
    if (len >= cap) return false;
    memcpy(out, p, len); out[len] = 0;
    return true;
  }
  return false;
}

static bool address(Download *d) {
  if (!endpoint_parse(d->url, &d->ep)) return false;
  const char *p = strchr(strstr(d->url, "://") + 3, '/');
  if (p && strlen(p) >= sizeof d->path) return false;
  snprintf(d->path, sizeof d->path, "%s", p ? p : "/");
  return !strpbrk(d->url, "\r\n");
}

static bool request(Download *d, Piece *p, long first, long last, long size,
                    Reply *reply, char *err, int cap) {
  memset(reply, 0, sizeof *reply);
  p->first = first; p->last = last; p->used = 0; p->active = true;
  if (!p->conn) p->conn = conn_open_download(&d->ep, 20000, err, cap);
  if (!p->conn) return false;
  char condition[224] = "";
  if (d->validator[0]) snprintf(condition, sizeof condition, "If-Range: %s\r\n", d->validator);
  int n = snprintf(d->request, sizeof d->request,
    "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: Tweebbo\r\nAccept-Encoding: identity\r\n"
    "Range: bytes=%ld-%ld\r\n%sConnection: keep-alive\r\n\r\n",
    d->path, d->ep.host, d->ep.port, first, last, condition);
  if (n < 0 || n >= (int)sizeof d->request || !conn_write(p->conn, d->request, n, 15000)) return false;
  int len = 0, end = 0;
  u64 deadline = now_ms() + 20000;
  while (!end && len < (int)sizeof d->head - 1 && now_ms() < deadline) {
    int got = conn_read(p->conn, d->head + len, (int)sizeof d->head - 1 - len, 100);
    if (got < 0) return false;
    int scan = len > 3 ? len - 3 : 0;
    len += got;
    for (int i = scan; i + 3 < len; i++) {
      if (!memcmp(d->head + i, "\r\n\r\n", 4)) { end = i + 4; break; }
    }
  }
  if (!end) return false;
  d->head[end-1] = 0;
  int minor = 0;
  if (sscanf(d->head, "HTTP/1.%d %d", &minor, &reply->status) != 2) return false;
  field(d->head, "Location", reply->location, sizeof reply->location);
  field(d->head, "ETag", reply->etag, sizeof reply->etag);
  field(d->head, "Last-Modified", reply->modified, sizeof reply->modified);
  if (reply->status != 206) return false;
  char length[48], range[96], value[192], extra;
  long a, b, total;
  if (!field(d->head, "Content-Range", range, sizeof range) ||
      sscanf(range, "bytes %ld-%ld/%ld%c", &a, &b, &total, &extra) != 3 ||
      a != first || b != last || total <= b || total <= 0 || (size > 0 && total != size)) return false;
  if (!field(d->head, "Content-Length", length, sizeof length)) return false;
  char *tail;
  errno = 0;
  long body = strtol(length, &tail, 10);
  if (errno || tail == length || *tail || body != last - first + 1) return false;
  if (field(d->head, "Transfer-Encoding", value, sizeof value)) return false;
  if (field(d->head, "Content-Encoding", value, sizeof value) && strcasecmp(value, "identity")) return false;
  field(d->head, "Connection", value, sizeof value);
  reply->keepalive = (minor == 1 && strcasecmp(value, "close")) || !strcasecmp(value, "keep-alive");
  if (!reply->keepalive) return false;
  if (d->validator[0] && (!field(d->head, d->validator_name, value, sizeof value) || strcmp(value, d->validator))) return false;
  reply->size = total;
  p->used = len - end;
  if (p->used > body) return false;
  memcpy(p->data, d->head + end, (size_t)p->used);
  return true;
}

static void release(Download *d) {
  for (int i = 0; i < CONNECTIONS; i++) { conn_close(d->pieces[i].conn); free(d->pieces[i].data); }
  free(d);
}

int http_download_optimized_from(const char *url, long from, int connections, HttpSink sink,
                                 void *ctx, long *total, char *err, int err_cap) {
  if (connections <= 1) return http_download_from(url, from, sink, ctx, total, err, err_cap);
  if (from < 0) { snprintf(err, (size_t)err_cap, "Invalid download offset."); return 0; }
  if (connections > CONNECTIONS) connections = CONNECTIONS;
  Download *d = calloc(1, sizeof *d);
  if (!d) return http_download_from(url, from, sink, ctx, total, err, err_cap);
  long committed = from;
  bool cancelled = false;
  if (strlen(url) >= sizeof d->url) goto fallback;
  snprintf(d->url, sizeof d->url, "%s", url);
  for (int i = 0; i < connections; i++) {
    d->pieces[i].data = malloc(PIECE);
    if (!d->pieces[i].data) goto fallback;
  }
  Reply reply;
  for (int hop = 0; ; hop++) {
    if (!address(d)) goto fallback;
    if (request(d, &d->pieces[0], 0, 0, 0, &reply, err, err_cap)) break;
    if (hop >= 5 || reply.status < 300 || reply.status >= 400 || !reply.location[0]) goto fallback;
    char redirect[2048];
    int n = reply.location[0] == '/' ? snprintf(redirect, sizeof redirect, "%s://%s:%d%s", d->ep.tls ? "https" : "http", d->ep.host, d->ep.port, reply.location)
                                    : snprintf(redirect, sizeof redirect, "%s", reply.location);
    if (n < 0 || n >= (int)sizeof redirect || (d->ep.tls && strncmp(redirect, "https://", 8))) goto fallback;
    snprintf(d->url, sizeof d->url, "%s", redirect);
    conn_close(d->pieces[0].conn); d->pieces[0].conn = NULL;
  }
  long size = reply.size;
  if (size - from < 1024 * 1024) goto fallback;
  if (reply.etag[0] == '"') {
    snprintf(d->validator_name, sizeof d->validator_name, "ETag");
    snprintf(d->validator, sizeof d->validator, "%s", reply.etag);
  } else if (reply.modified[0]) {
    snprintf(d->validator_name, sizeof d->validator_name, "Last-Modified");
    snprintf(d->validator, sizeof d->validator, "%s", reply.modified);
  } else goto fallback;
  // Consume the probe byte before reusing its TCP/TLS connection.
  u64 deadline = now_ms() + 20000;
  while (!d->pieces[0].used) {
    int n = conn_read(d->pieces[0].conn, d->pieces[0].data, 1, 100);
    if (n < 0 || now_ms() > deadline) goto fallback;
    d->pieces[0].used += n;
  }
  *total = size;
  long next = from;
  for (int i = 0; i < connections; i++) {
    long last = size - next > PIECE ? next + PIECE - 1 : size - 1;
    if (!request(d, &d->pieces[i], next, last, size, &reply, err, err_cap)) goto fallback;
    next = last + 1;
  }
  int current = 0;
  u64 progress = now_ms();
  while (committed < size) {
    bool advanced = false;
    for (int i = 0; i < connections; i++) {
      Piece *p = &d->pieces[i];
      int left = (int)(p->last - p->first + 1) - p->used;
      if (!p->active || left <= 0) continue;
      int n = conn_read(p->conn, p->data + p->used, left, 0);
      if (n < 0) goto fallback;
      p->used += n;
      if (n) { advanced = true; progress = now_ms(); }
    }
    Piece *p = &d->pieces[current];
    if (p->active && p->used == p->last - p->first + 1) {
      if (!sink(p->data, p->used, ctx)) { cancelled = true; goto fallback; }
      committed += p->used;
      p->active = false;
      if (next < size) {
        long last = size - next > PIECE ? next + PIECE - 1 : size - 1;
        if (!request(d, p, next, last, size, &reply, err, err_cap)) goto fallback;
        next = last + 1;
      }
      current = (current + 1) % connections;
      advanced = true; progress = now_ms();
    }
    if (now_ms() - progress > 30000) goto fallback;
    if (!advanced) pause_io();
  }
  release(d);
  err[0] = 0;
  return from ? 206 : 200;

fallback:
  release(d);
  if (cancelled) { snprintf(err, (size_t)err_cap, "Download stopped."); return 0; }
  // Only committed, ordered bytes reached the sink; prefetched ranges can be discarded.
  return http_download_from(url, committed, sink, ctx, total, err, err_cap);
}
