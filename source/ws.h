// WebSocket client (RFC 6455) and HTTP/1.1 requests on top of conn.c.
#pragma once
#include "conn.h"

typedef struct {
  Conn *conn;
  u8 *rx;
  int rx_len, rx_cap;
  u8 *msg; /* fragmented message in progress */
  int msg_len, msg_cap;
} Ws;

typedef void (*WsHandler)(const u8 *data, int len, void *ctx);

/** Connects and completes the HTTP Upgrade handshake on `path` (e.g. /ws/lobby). */
bool ws_connect(Ws *ws, const Endpoint *ep, const char *path, int timeout_ms, char *err, int err_cap);
/** Sends a binary message in a single frame (masked, as the protocol requires). */
bool ws_send(Ws *ws, const u8 *data, int len);
/** Reads what is available and calls `fn` for each binary message. Returns false when the connection ended. */
bool ws_poll(Ws *ws, int timeout_ms, WsHandler fn, void *ctx);
void ws_close(Ws *ws);

typedef struct {
  int status;   /* 0 if there was no answer */
  char *body;   /* NUL-terminated; release with free() */
  int len;
  char err[96];
} HttpResult;

/** Blocking request. `bearer` and `json` may be NULL. */
void http_request(const Endpoint *ep, const char *method, const char *path, const char *bearer, const char *json,
                  int timeout_ms, HttpResult *out);

/** Escapes `s` as a JSON string (without the outer quotes). */
void json_escape(const char *s, char *out, int cap);

/** Receives body bytes as they arrive; returning false cancels the download. */
typedef bool (*HttpSink)(const u8 *data, int len, void *ctx);

/**
 * Downloads `url` (http/https, up to 5 redirects), handing the body to `sink`.
 * `total` receives Content-Length (or -1). Returns the final HTTP status (0 if there was no answer).
 */
int http_download(const char *url, HttpSink sink, void *ctx, long *total, char *err, int err_cap);
/** Same, resuming at byte `from` (Range): the sink gets the bytes after `from`, `total` is the whole
 *  size, and a body cut short of Content-Length returns 0. Status 206 or 200. */
int http_download_from(const char *url, long from, HttpSink sink, void *ctx, long *total, char *err, int err_cap);
