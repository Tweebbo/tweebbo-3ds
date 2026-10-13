#include "conn.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
#endif

#ifndef NO_TLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include "certs.h"
#endif

struct Conn {
  int fd;
  bool tls;
#ifndef NO_TLS
  mbedtls_ssl_context ssl;
#endif
};

#ifndef NO_TLS
static struct {
  bool ready;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_x509_crt roots;
  mbedtls_ssl_config conf;
} T;
#endif

#ifdef __3DS__
static LightLock tls_lock = 1;
#define TLS_LOCK() LightLock_Lock(&tls_lock)
#define TLS_UNLOCK() LightLock_Unlock(&tls_lock)
#else
#include <pthread.h>
static pthread_mutex_t tls_lock = PTHREAD_MUTEX_INITIALIZER;
#define TLS_LOCK() pthread_mutex_lock(&tls_lock)
#define TLS_UNLOCK() pthread_mutex_unlock(&tls_lock)
#endif
static bool sockets_ready;
bool conn_ready(void) { return sockets_ready; }

#ifdef __3DS__
static u32 *soc_buf;
static bool sslc_ready;
#define SOC_SIZE 0x100000
#endif

bool endpoint_parse(const char *url, Endpoint *out) {
  memset(out, 0, sizeof *out);
  const char *p = strstr(url, "://");
  if (!p) return false;
  int scheme = (int)(p - url);
  if ((scheme == 3 && !strncmp(url, "wss", 3)) || (scheme == 5 && !strncmp(url, "https", 5))) out->tls = true;
  else if ((scheme == 2 && !strncmp(url, "ws", 2)) || (scheme == 4 && !strncmp(url, "http", 4))) out->tls = false;
  else return false;
  p += 3;
  int n = 0;
  while (*p && *p != ':' && *p != '/' && n < (int)sizeof out->host - 1) out->host[n++] = *p++;
  out->host[n] = 0;
  out->port = out->tls ? 443 : 80;
  if (*p == ':') out->port = atoi(p + 1);
  return n > 0 && out->port > 0 && out->port < 65536;
}

#ifndef NO_TLS
/** Own entropy source: sslc (like devkitPro), then ps:ps and, as a last resort, the system clock. */
static int entropy(void *ctx, unsigned char *out, size_t len) {
  (void)ctx;
#ifdef __3DS__
  if (sslc_ready && R_SUCCEEDED(sslcGenerateRandomData(out, len))) return 0;
  if (R_SUCCEEDED(psInit())) {
    Result r = PS_GenerateRandomBytes(out, len);
    psExit();
    if (R_SUCCEEDED(r)) return 0;
  }
  u64 x = svcGetSystemTick() ^ osGetTime() * 0x9E3779B97F4A7C15ull;
  for (size_t i = 0; i < len; i++) { x ^= svcGetSystemTick(); x ^= x << 13; x ^= x >> 7; x ^= x << 17; out[i] = (u8)(x >> 24); }
  return 0;
#else
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f || fread(out, 1, len, f) != len) { if (f) fclose(f); return -1; }
  fclose(f);
  return 0;
#endif
}

static bool tls_init(char *err, int err_cap) {
  if (T.ready) return true;
#ifdef __3DS__
  if (!sslc_ready) sslc_ready = R_SUCCEEDED(sslcInit(0));
#endif
  mbedtls_ctr_drbg_init(&T.drbg);
  mbedtls_x509_crt_init(&T.roots);
  mbedtls_ssl_config_init(&T.conf);
  const char *who = "tweebbo-3ds";
  int r = mbedtls_ctr_drbg_seed(&T.drbg, entropy, NULL, (const u8 *)who, strlen(who));
  if (r) { snprintf(err, (size_t)err_cap, "Secure random generator failed (-0x%04x).", (unsigned)-r); return false; }
  r = mbedtls_x509_crt_parse(&T.roots, (const u8 *)TRUSTED_ROOTS, strlen(TRUSTED_ROOTS) + 1);
  if (r < 0) { snprintf(err, (size_t)err_cap, "Certificates could not be loaded (-0x%04x).", (unsigned)-r); return false; }
  mbedtls_ssl_config_defaults(&T.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  mbedtls_ssl_conf_authmode(&T.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&T.conf, &T.roots, NULL);
  mbedtls_ssl_conf_rng(&T.conf, mbedtls_ctr_drbg_random, &T.drbg);
  T.ready = true;
  return true;
}
#endif

bool conn_global_init(char *err, int err_cap) {
  err[0] = 0;
#ifdef __3DS__
  if (!soc_buf) {
    soc_buf = memalign(0x1000, SOC_SIZE);
    if (!soc_buf) { snprintf(err, (size_t)err_cap, "Not enough memory for the network."); return false; }
    Result r = socInit(soc_buf, SOC_SIZE);
    if (R_FAILED(r)) {
      free(soc_buf);
      soc_buf = NULL;
      snprintf(err, (size_t)err_cap, "Network service unavailable (0x%08lX). Is Wi-Fi on?", (unsigned long)r);
      return false;
    }
  }
#endif
  sockets_ready = true;
  return true;
}

void conn_global_exit(void) {
#ifndef NO_TLS
  if (T.ready) {
    mbedtls_ssl_config_free(&T.conf);
    mbedtls_x509_crt_free(&T.roots);
    mbedtls_ctr_drbg_free(&T.drbg);
    T.ready = false;
  }
#endif
#ifdef __3DS__
  if (sslc_ready) { sslcExit(); sslc_ready = false; }
  if (soc_buf) { socExit(); free(soc_buf); soc_buf = NULL; }
  sockets_ready = false;
#endif
}

static bool wait_fd(int fd, short events, int timeout_ms) {
  struct pollfd p = {.fd = fd, .events = events};
  return poll(&p, 1, timeout_ms) > 0 && (p.revents & (events | POLLERR | POLLHUP));
}

static bool would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }

#ifndef NO_TLS
static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
  int n = (int)send(((Conn *)ctx)->fd, buf, len, 0);
  if (n >= 0) return n;
  return would_block() ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
  int n = (int)recv(((Conn *)ctx)->fd, buf, len, 0);
  if (n > 0) return n;
  if (n == 0) return MBEDTLS_ERR_NET_CONN_RESET;
  return would_block() ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
}
#endif

Conn *conn_open(const Endpoint *ep, int timeout_ms, char *err, int err_cap) {
  err[0] = 0;
  char port[8];
  snprintf(port, sizeof port, "%d", ep->port);
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res = NULL;
  if (getaddrinfo(ep->host, port, &hints, &res) || !res) {
    snprintf(err, (size_t)err_cap, "Could not find %s.", ep->host);
    return NULL;
  }
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (fd < 0) { freeaddrinfo(res); snprintf(err, (size_t)err_cap, "No network (socket error %d).", errno); return NULL; }
#ifdef __3DS__
  // On the console, use a blocking connect: polling SO_ERROR in non-blocking mode is not reliable
  // on every firmware. The network thread can wait; drawing continues on the main thread.
  (void)timeout_ms;
  int r = connect(fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (r < 0) {
    int e = errno;
    close(fd);
    snprintf(err, (size_t)err_cap, "Could not reach %s:%d (error %d). Same Wi-Fi as the server?", ep->host, ep->port, e);
    return NULL;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#else
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  int r = connect(fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (r < 0 && !would_block()) { close(fd); snprintf(err, (size_t)err_cap, "Connection refused (%d).", errno); return NULL; }
  if (r < 0) {
    int so = 0;
    socklen_t sl = sizeof so;
    if (!wait_fd(fd, POLLOUT, timeout_ms) || getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &sl) || so) {
      close(fd);
      snprintf(err, (size_t)err_cap, "Could not reach %s:%d (error %d).", ep->host, ep->port, so);
      return NULL;
    }
  }
#endif
#ifndef NO_TLS
  if (ep->tls) {
    TLS_LOCK();
    bool ok = tls_init(err, err_cap);
    TLS_UNLOCK();
    if (!ok) { close(fd); return NULL; }
  }
#endif
  Conn *c = calloc(1, sizeof *c);
  c->fd = fd;
  c->tls = ep->tls;
  if (!c->tls) return c;
#ifdef NO_TLS
  snprintf(err, (size_t)err_cap, "Secure connections are not available.");
  close(fd); free(c);
  return NULL;
#else
  mbedtls_ssl_init(&c->ssl);
  if (mbedtls_ssl_setup(&c->ssl, &T.conf) || mbedtls_ssl_set_hostname(&c->ssl, ep->host)) {
    snprintf(err, (size_t)err_cap, "Secure connection setup failed.");
    conn_close(c);
    return NULL;
  }
  mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);
  int spent = 0;
  for (;;) {
    r = mbedtls_ssl_handshake(&c->ssl);
    if (!r) break;
    if ((r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) || spent > timeout_ms) {
      u32 flags = mbedtls_ssl_get_verify_result(&c->ssl);
      if (flags && flags != (u32)-1) snprintf(err, (size_t)err_cap, "The server certificate is not trusted (check the console date).");
      else snprintf(err, (size_t)err_cap, "Secure connection failed (-0x%04x).", (unsigned)-r);
      conn_close(c);
      return NULL;
    }
    wait_fd(fd, r == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, 100);
    spent += 100;
  }
  return c;
#endif
}

void conn_close(Conn *c) {
  if (!c) return;
#ifndef NO_TLS
  if (c->tls) {
    mbedtls_ssl_close_notify(&c->ssl);
    mbedtls_ssl_free(&c->ssl);
  }
#endif
  close(c->fd);
  free(c);
}

bool conn_write(Conn *c, const void *data, int len, int timeout_ms) {
  const u8 *p = data;
  int spent = 0;
  while (len > 0) {
    int n;
#ifndef NO_TLS
    if (c->tls) {
      n = mbedtls_ssl_write(&c->ssl, p, (size_t)len);
      if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) n = -2;
      else if (n < 0) return false;
    } else
#endif
    {
      n = (int)send(c->fd, p, (size_t)len, 0);
      if (n < 0) { if (!would_block()) return false; n = -2; }
    }
    if (n == -2) {
      if (spent > timeout_ms) return false;
      wait_fd(c->fd, POLLOUT, 50);
      spent += 50;
      continue;
    }
    p += n;
    len -= n;
  }
  return true;
}

int conn_read(Conn *c, void *buf, int cap, int timeout_ms) {
#ifndef NO_TLS
  if (c->tls && mbedtls_ssl_get_bytes_avail(&c->ssl) == 0 && !wait_fd(c->fd, POLLIN, timeout_ms)) return 0;
  if (c->tls) {
    int n = mbedtls_ssl_read(&c->ssl, buf, (size_t)cap);
    if (n > 0) return n;
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    return -1;
  }
#endif
  if (!wait_fd(c->fd, POLLIN, timeout_ms)) return 0;
  int n = (int)recv(c->fd, buf, (size_t)cap, 0);
  if (n > 0) return n;
  if (n < 0 && would_block()) return 0;
  return -1;
}
