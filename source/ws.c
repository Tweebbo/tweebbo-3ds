#include "ws.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#define MAX_MESSAGE (2 * 1024 * 1024)

static void base64(const u8 *in, int n, char *out) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    u32 v = (u32)in[i] << 16 | (i + 1 < n ? (u32)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = T[v >> 18 & 63];
    out[o++] = T[v >> 12 & 63];
    out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? T[v & 63] : '=';
  }
  out[o] = 0;
}

static bool grow(u8 **buf, int *cap, int need) {
  if (need <= *cap) return true;
  if (need > MAX_MESSAGE + 16) return false;
  int n = *cap ? *cap : 4096;
  while (n < need) n *= 2;
  u8 *p = realloc(*buf, (size_t)n);
  if (!p) return false;
  *buf = p;
  *cap = n;
  return true;
}

/** Reads up to the end of the HTTP headers. Leaves any extra bytes in ws->rx. */
static int read_headers(Conn *c, u8 **buf, int *len, int *cap, int timeout_ms) {
  int spent = 0;
  for (;;) {
    for (int i = 3; i < *len; i++)
      if ((*buf)[i - 3] == '\r' && (*buf)[i - 2] == '\n' && (*buf)[i - 1] == '\r' && (*buf)[i] == '\n') return i + 1;
    if (!grow(buf, cap, *len + 2048) || *len > 32768) return -1;
    int n = conn_read(c, *buf + *len, *cap - *len - 1, 100);
    if (n < 0) return -1;
    if (n == 0 && (spent += 100) > timeout_ms) return -1;
    *len += n;
  }
}

bool ws_connect(Ws *ws, const Endpoint *ep, const char *path, int timeout_ms, char *err, int err_cap) {
  memset(ws, 0, sizeof *ws);
  ws->conn = conn_open(ep, timeout_ms, err, err_cap);
  if (!ws->conn) return false;
  u8 nonce[16];
  for (int i = 0; i < 16; i++) nonce[i] = (u8)rnd_range(0, 256);
  char key[32], req[512], host[112];
  base64(nonce, 16, key);
  if ((ep->tls && ep->port == 443) || (!ep->tls && ep->port == 80)) snprintf(host, sizeof host, "%s", ep->host);
  else snprintf(host, sizeof host, "%s:%d", ep->host, ep->port);
  int n = snprintf(req, sizeof req,
                   "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: Tweebbo-3DS\r\n\r\n",
                   path, host, key);
  if (!conn_write(ws->conn, req, n, timeout_ms)) { snprintf(err, (size_t)err_cap, "Connection lost."); ws_close(ws); return false; }
  int end = read_headers(ws->conn, &ws->rx, &ws->rx_len, &ws->rx_cap, timeout_ms);
  if (end < 0) { snprintf(err, (size_t)err_cap, "The server did not answer."); ws_close(ws); return false; }
  ws->rx[ws->rx_len] = 0;
  int status = 0;
  sscanf((char *)ws->rx, "HTTP/1.%*d %d", &status);
  if (status != 101) {
    if (status == 404) snprintf(err, (size_t)err_cap, "This room does not exist.");
    else snprintf(err, (size_t)err_cap, "The server refused the connection (%d).", status);
    ws_close(ws);
    ws->http_status = status;
    return false;
  }
  ws->http_status = status;
  memmove(ws->rx, ws->rx + end, (size_t)(ws->rx_len - end));
  ws->rx_len -= end;
  return true;
}

static bool send_frame(Ws *ws, u8 opcode, const u8 *data, int len) {
  u8 head[14];
  int h = 0;
  head[h++] = 0x80 | opcode;
  if (len < 126) head[h++] = 0x80 | (u8)len;
  else if (len < 65536) { head[h++] = 0x80 | 126; head[h++] = (u8)(len >> 8); head[h++] = (u8)len; }
  else {
    head[h++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--) head[h++] = i < 4 ? (u8)(len >> (i * 8)) : 0;
  }
  u8 mask[4];
  for (int i = 0; i < 4; i++) mask[i] = head[h++] = (u8)rnd_range(0, 256);
  u8 *frame = malloc((size_t)(h + len));
  if (!frame) return false;
  memcpy(frame, head, (size_t)h);
  for (int i = 0; i < len; i++) frame[h + i] = data[i] ^ mask[i & 3];
  bool ok = conn_write(ws->conn, frame, h + len, 5000);
  free(frame);
  return ok;
}

bool ws_send(Ws *ws, const u8 *data, int len) { return ws->conn && send_frame(ws, 0x2, data, len); }
bool ws_send_text(Ws *ws, const u8 *data, int len) { return ws->conn && send_frame(ws, 0x1, data, len); }

/** Processes complete frames in the buffer. false on a close frame or a protocol error. */
static bool parse(Ws *ws, WsHandler fn, void *ctx) {
  for (;;) {
    if (ws->rx_len < 2) return true;
    u8 *b = ws->rx;
    bool fin = b[0] & 0x80, masked = b[1] & 0x80;
    int op = b[0] & 15, h = 2;
    u64 len = b[1] & 127;
    if (len == 126) { if (ws->rx_len < 4) return true; len = (u64)b[2] << 8 | b[3]; h = 4; }
    else if (len == 127) {
      if (ws->rx_len < 10) return true;
      len = 0;
      for (int i = 0; i < 8; i++) len = len << 8 | b[2 + i];
      h = 10;
    }
    if (len > (ws->text_messages ? 65536u : MAX_MESSAGE)) return false;
    u8 mask[4] = {0};
    if (masked) { if (ws->rx_len < h + 4) return true; memcpy(mask, b + h, 4); h += 4; }
    if (ws->rx_len < h + (int)len) {
      if (!grow(&ws->rx, &ws->rx_cap, h + (int)len + 1)) return false;
      return true;
    }
    u8 *p = b + h;
    if (masked) for (u64 i = 0; i < len; i++) p[i] ^= mask[i & 3];
    if (op == 0x8) return false;
    if (op == 0x9) send_frame(ws, 0xA, p, (int)len);
    else if (op == (ws->text_messages ? 0x1 : 0x2) || op == 0x0) {
      if (op == (ws->text_messages ? 0x1 : 0x2) && fin && !ws->msg_len) fn(p, (int)len, ctx);
      else {
        if(ws->text_messages && ws->msg_len + len > 65536u)return false;
        if (!grow(&ws->msg, &ws->msg_cap, ws->msg_len + (int)len)) return false;
        memcpy(ws->msg + ws->msg_len, p, (size_t)len);
        ws->msg_len += (int)len;
        if (fin) { fn(ws->msg, ws->msg_len, ctx); ws->msg_len = 0; }
      }
    }
    int used = h + (int)len;
    memmove(ws->rx, ws->rx + used, (size_t)(ws->rx_len - used));
    ws->rx_len -= used;
  }
}

bool ws_poll(Ws *ws, int timeout_ms, WsHandler fn, void *ctx) {
  if (!ws->conn) return false;
  for (int round = 0; round < 64; round++) {
    if (!grow(&ws->rx, &ws->rx_cap, ws->rx_len + 16384)) return false;
    int n = conn_read(ws->conn, ws->rx + ws->rx_len, ws->rx_cap - ws->rx_len, round ? 0 : timeout_ms);
    if (n < 0) return false;
    if (n == 0) break;
    ws->rx_len += n;
    if (!parse(ws, fn, ctx)) return false;
  }
  return true;
}

void ws_close(Ws *ws) {
  if (ws->conn) {
    u8 code[2] = {0x03, 0xE8};
    send_frame(ws, 0x8, code, 2);
    conn_close(ws->conn);
  }
  free(ws->rx);
  free(ws->msg);
  memset(ws, 0, sizeof *ws);
}

// ---------------------------------------------------------------- HTTP

void json_escape(const char *s, char *out, int cap) {
  int o = 0;
  for (; *s && o < cap - 7; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = (char)ch; }
    else if (ch < 0x20) o += snprintf(out + o, (size_t)(cap - o), "\\u%04x", ch);
    else out[o++] = (char)ch;
  }
  out[o] = 0;
}

static char *header(char *head, const char *name) {
  size_t n = strlen(name);
  for (char *p = strstr(head, "\r\n"); p; p = strstr(p + 2, "\r\n"))
    if (!strncasecmp(p + 2, name, n) && p[2 + n] == ':') { p += 3 + n; while (*p == ' ') p++; return p; }
  return NULL;
}

void http_request(const Endpoint *ep, const char *method, const char *path, const char *bearer, const char *json,
                  int timeout_ms, HttpResult *out) {
  memset(out, 0, sizeof *out);
  Conn *c = conn_open(ep, timeout_ms, out->err, sizeof out->err);
  if (!c) return;
  char host[112], auth[160] = "", type[64] = "";
  if ((ep->tls && ep->port == 443) || (!ep->tls && ep->port == 80)) snprintf(host, sizeof host, "%s", ep->host);
  else snprintf(host, sizeof host, "%s:%d", ep->host, ep->port);
  if (bearer && *bearer) snprintf(auth, sizeof auth, "Authorization: Bearer %s\r\n", bearer);
  int body_len = json ? (int)strlen(json) : 0;
  if (json) snprintf(type, sizeof type, "Content-Type: application/json\r\n");
  char req[1024];
  int n = snprintf(req, sizeof req,
                   "%s %s HTTP/1.1\r\nHost: %s\r\n%s%sContent-Length: %d\r\nAccept: application/json\r\n"
                   "Cache-Control: no-store\r\nUser-Agent: Tweebbo-3DS\r\nConnection: close\r\n\r\n",
                   method, path, host, auth, type, body_len);
  if (!conn_write(c, req, n, timeout_ms) || (json && !conn_write(c, json, body_len, timeout_ms))) {
    snprintf(out->err, sizeof out->err, "Connection lost.");
    conn_close(c);
    return;
  }
  u8 *buf = NULL;
  int len = 0, cap = 0, spent = 0;
  for (;;) {
    if (!grow(&buf, &cap, len + 8192)) break;
    int r = conn_read(c, buf + len, cap - len - 1, 100);
    if (r < 0) break;
    if (r == 0) { if ((spent += 100) > timeout_ms) break; continue; }
    len += r;
    buf[len] = 0;
    // With Content-Length we can finish without waiting for the close.
    char *end = strstr((char *)buf, "\r\n\r\n");
    if (end) {
      *end = 0;
      char *cl = header((char *)buf, "Content-Length");
      int want = cl ? atoi(cl) : -1;
      *end = '\r';
      if (want >= 0 && len >= (int)(end + 4 - (char *)buf) + want) break;
    }
  }
  conn_close(c);
  if (!buf) { snprintf(out->err, sizeof out->err, "Out of memory."); return; }
  buf[len] = 0;
  char *end = strstr((char *)buf, "\r\n\r\n");
  if (!end || sscanf((char *)buf, "HTTP/1.%*d %d", &out->status) != 1) {
    snprintf(out->err, sizeof out->err, "The server did not answer.");
    free(buf);
    return;
  }
  *end = 0;
  char *body = end + 4;
  int blen = len - (int)(body - (char *)buf);
  char *te = header((char *)buf, "Transfer-Encoding");
  if (te && !strncasecmp(te, "chunked", 7)) {
    // Reassembles the chunked body in the same buffer.
    char *src = body, *dst = body, *lim = body + blen;
    while (src < lim) {
      long size = strtol(src, NULL, 16);
      char *data = strstr(src, "\r\n");
      if (!data || size <= 0) break;
      data += 2;
      if (data + size > lim) size = lim - data;
      memmove(dst, data, (size_t)size);
      dst += size;
      src = data + size + 2;
    }
    blen = (int)(dst - body);
  }
  out->body = malloc((size_t)blen + 1);
  if (out->body) { memcpy(out->body, body, (size_t)blen); out->body[blen] = 0; out->len = blen; }
  free(buf);
}

// ---------------------------------------------------------------- large downloads (updates)

static bool split_url(const char *url, Endpoint *ep, char *path, int cap) {
  if (!endpoint_parse(url, ep)) return false;
  const char *p = strstr(url, "://") + 3;
  p = strchr(p, '/');
  snprintf(path, (size_t)cap, "%s", p ? p : "/");
  return true;
}

int http_download_from(const char *url, long from, HttpSink sink, void *ctx, long *total, char *err, int err_cap) {
  // Signed redirect URLs (GitHub release assets) are close to 1 KB.
  char current[2048], next[2048], path[2048], req[2600];
  *total = -1;
  if (from < 0 || strlen(url) >= sizeof current) { snprintf(err, (size_t)err_cap, "Invalid download request."); return 0; }
  snprintf(current, sizeof current, "%s", url);
  err[0] = 0;
  *total = -1;
  for (int hop = 0; hop < 6; hop++) {
    Endpoint ep;
    if (!split_url(current, &ep, path, sizeof path)) { snprintf(err, (size_t)err_cap, "Invalid address."); return 0; }
    Conn *c = conn_open_download(&ep, 20000, err, err_cap);
    if (!c) return 0;
    // HTTP/1.0: no chunked encoding, the body arrives as-is until the connection closes.
    char range[48] = "";
    if (from > 0) snprintf(range, sizeof range, "Range: bytes=%ld-\r\n", from);
    int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: Tweebbo-3DS\r\nAccept: */*\r\nAccept-Encoding: identity\r\n%sConnection: close\r\n\r\n",
                     path, ep.host, ep.port, range);
    if (!conn_write(c, req, n, 15000)) { conn_close(c); snprintf(err, (size_t)err_cap, "Connection lost."); return 0; }
    u8 *head = NULL;
    int len = 0, cap = 0;
    int end = read_headers(c, &head, &len, &cap, 20000);
    if (end < 0) { conn_close(c); free(head); snprintf(err, (size_t)err_cap, "The server did not answer."); return 0; }
    char saved = (char)head[end - 1];
    head[end - 1] = 0;
    int status = 0;
    sscanf((char *)head, "HTTP/1.%*d %d", &status);
    if (status < 100 || status > 599) {
      char first[80];
      snprintf(first, sizeof first, "%.79s", (char *)head);
      for (char *q = first; *q; q++) if (*q == '\r' || *q == '\n') *q = ' ';
      dbg_log("http_download: unexpected response from %s: %s", ep.host, first);
    }
    if (status >= 300 && status < 400) {
      char *loc = header((char *)head, "Location");
      if (!loc) { conn_close(c); free(head); snprintf(err, (size_t)err_cap, "Redirect without location."); return status; }
      int k = 0;
      while (loc[k] && loc[k] != '\r' && loc[k] != '\n' && k < (int)sizeof next - 1) { next[k] = loc[k]; k++; }
      next[k] = 0;
      if (next[0] == '/') snprintf(current, sizeof current, "%s://%s:%d%s", ep.tls ? "https" : "http", ep.host, ep.port, next);
      else snprintf(current, sizeof current, "%s", next);
      conn_close(c);
      free(head);
      continue;
    }
    char *cl = header((char *)head, "Content-Length");
    char *tail = NULL;
    errno = 0;
    long body = cl ? strtol(cl, &tail, 10) : -1;
    bool framing = !cl || (!errno && tail != cl && (*tail == '\r' || *tail == '\n' || !*tail) && body >= 0);
    if (status == 206) {
      long first, last, size;
      char *cr = header((char *)head, "Content-Range");
      framing = framing && cr && body > 0 && sscanf(cr, "bytes %ld-%ld/%ld", &first, &last, &size) == 3 &&
                first == from && last >= first && size > last && last == size - 1 && last - first + 1 == body;
    }
    if (header((char *)head, "Transfer-Encoding")) framing = false;
    if (!framing) { free(head); conn_close(c); snprintf(err, (size_t)err_cap, "Invalid download response."); return 0; }
    // 206 continues at `from`; a server that ignores Range sends everything again (200): skip
    // what the caller already has, so the sink always receives the bytes after `from`.
    long skip = status == 200 ? from : 0;
    if (body >= 0) *total = status == 206 ? from + body : body;
    head[end - 1] = (u8)saved;
    bool ok = status == 200 || (status == 206 && from > 0);
    if (!ok) { free(head); conn_close(c); snprintf(err, (size_t)err_cap, "The server answered %d.", status); return status; }
    long got = 0;
    u8 *buf = malloc(32768);
    if (!buf) { free(head); conn_close(c); snprintf(err, (size_t)err_cap, "Not enough memory to download."); return 0; }
    int idle = 0;
    u8 *data = head + end;
    int r = len - end;
    while (ok && buf) {
      if (r > 0) {
        if (body >= 0 && r > body - got) { snprintf(err, (size_t)err_cap, "Invalid download length."); ok = false; break; }
        got += r;
        int drop = skip > r ? r : (int)skip;
        skip -= drop;
        if (r > drop) ok = sink(data + drop, r - drop, ctx);
      }
      if (!ok) break;
      if (body >= 0 && got == body) break;
      r = conn_read(c, buf, 32768, 200);
      data = buf;
      if (r < 0) break;
      if (r == 0) { if ((idle += 200) > 30000) { snprintf(err, (size_t)err_cap, "The download stalled."); ok = false; } continue; }
      idle = 0;
    }
    free(head);
    free(buf);
    conn_close(c);
    // HTTP/1.0 ends at the close: a body shorter than Content-Length was cut off.
    if (ok && body >= 0 && got < body) { snprintf(err, (size_t)err_cap, "The download was interrupted."); ok = false; }
    return ok ? status : 0;
  }
  snprintf(err, (size_t)err_cap, "Too many redirects.");
  return 0;
}

int http_download(const char *url, HttpSink sink, void *ctx, long *total, char *err, int err_cap) {
  return http_download_from(url, 0, sink, ctx, total, err, err_cap);
}
