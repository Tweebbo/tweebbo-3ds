// TCP connection with optional TLS (mbedTLS) and non-blocking sockets.
// Works the same on the console (libctru soc:U) and on the PC (tests).
#pragma once
#include "common.h"

/** Server address: ws(s)://host[:port] or http(s)://host[:port]. */
typedef struct {
  bool tls;
  char host[96];
  int port;
} Endpoint;

bool endpoint_parse(const char *url, Endpoint *out);

typedef struct Conn Conn;

/** Initializes sockets (TLS is set up on the first secure connection). `err` explains a failure. */
bool conn_global_init(char *err, int err_cap);
void conn_global_exit(void);
/** true once the sockets are initialized. */
bool conn_ready(void);

/** Connects (and negotiates TLS) with a time limit. NULL on failure; `err` describes why. */
Conn *conn_open(const Endpoint *ep, int timeout_ms, char *err, int err_cap);
void conn_close(Conn *c);
/** Writes everything or fails (false). */
bool conn_write(Conn *c, const void *data, int len, int timeout_ms);
/** Reads what is available, waiting at most `timeout_ms`. >0 bytes, 0 nothing yet, <0 connection closed. */
int conn_read(Conn *c, void *buf, int cap, int timeout_ms);
