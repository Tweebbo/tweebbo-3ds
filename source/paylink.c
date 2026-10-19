// Console pay links (see paylink.h). The server issues the link only for this character's
// own token, the link is accepted only by the payment routes, and every capture is verified
// with PayPal and credited exactly once by the server; nothing here can add credits or chips.
#include "paylink.h"
#include "net.h"
#include "../vendor/qrcodegen/qrcodegen.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLL_MS 4000
#define LINK_MS (15 * 60 * 1000)

static struct {
  PayState state;
  int job, poll_job;
  char kind[8], token[48], message[160];
  u64 expires, next_poll;
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
} P;

static void fail(const char *text) {
  P.state = PAY_ERROR;
  snprintf(P.message, sizeof P.message, "%s", text);
}

void paylink_open(const char *kind, u64 now) {
  (void)now;
  memset(&P, 0, sizeof P);
  snprintf(P.kind, sizeof P.kind, "%s", kind);
  if (!net_online() || !net_token()[0]) { fail("Connect to the hotel first."); return; }
  char body[48];
  snprintf(body, sizeof body, "{\"kind\":\"%s\"}", P.kind);
  P.job = net_http("POST", "/api/pay-link", body, true);
  P.state = P.job ? PAY_LOADING : PAY_ERROR;
  if (!P.job) fail("The hotel is busy. Try again in a moment.");
}

void paylink_close(void) {
  memset(P.token, 0, sizeof P.token);
  P.state = PAY_IDLE;
  P.job = P.poll_job = 0;
}

static const char *error_of(json_t *j, const char *fallback) {
  const char *e = j ? json_string_value(json_object_get(j, "error")) : NULL;
  return e && *e ? e : fallback;
}

static void got_link(HttpResult *r, u64 now) {
  json_error_t err;
  json_t *j = r->body ? json_loads(r->body, 0, &err) : NULL;
  const char *url = j ? json_string_value(json_object_get(j, "url")) : NULL;
  const char *token = j ? json_string_value(json_object_get(j, "token")) : NULL;
  if (r->status != 200 || !url || !token || strlen(token) >= sizeof P.token) {
    fail(error_of(j, "Could not make a payment code. Try again in a moment."));
  } else {
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
    if (!qrcodegen_encodeText(url, temp, P.qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 8, qrcodegen_Mask_AUTO, true)) {
      fail("Could not draw the payment code.");
    } else {
      snprintf(P.token, sizeof P.token, "%s", token);
      P.expires = now + LINK_MS;
      P.next_poll = now + POLL_MS;
      P.state = PAY_READY;
    }
  }
  if (j) json_decref(j);
}

static void got_status(HttpResult *r) {
  json_error_t err;
  json_t *j = r->body && r->status == 200 ? json_loads(r->body, 0, &err) : NULL;
  if (j && json_is_true(json_object_get(j, "paid"))) {
    P.state = PAY_PAID;
    if (!strcmp(P.kind, "club"))
      snprintf(P.message, sizeof P.message, "Thank you! You are a Tweebbo Club member for life. Your badges appear the next time you enter a room.");
    else
      snprintf(P.message, sizeof P.message, "Payment received! You now have %lld chips.", (long long)json_integer_value(json_object_get(j, "chips")));
  } else if (r->status == 404) {
    P.state = PAY_EXPIRED;
  }
  if (j) json_decref(j);
}

void paylink_poll(u64 now) {
  HttpResult r;
  if (P.job && net_http_result(P.job, &r)) { P.job = 0; got_link(&r, now); free(r.body); }
  if (P.poll_job && net_http_result(P.poll_job, &r)) { P.poll_job = 0; got_status(&r); free(r.body); }
  if (P.state != PAY_READY) return;
  // Keep checking a little after the code expires: a payment started in time may still land.
  if (now > P.expires + 5 * 60 * 1000) { P.state = PAY_EXPIRED; return; }
  if (!P.poll_job && now >= P.next_poll) {
    char path[96];
    snprintf(path, sizeof path, "/api/pay-link?token=%s", P.token);
    P.poll_job = net_http("GET", path, NULL, false);
    P.next_poll = now + POLL_MS;
  }
}

PayState paylink_state(void) { return P.state; }
const char *paylink_message(void) { return P.message; }
int paylink_seconds_left(u64 now) { return P.state == PAY_READY && P.expires > now ? (int)((P.expires - now) / 1000) : 0; }
int paylink_qr_size(void) { return P.state == PAY_READY ? qrcodegen_getSize(P.qr) : 0; }
bool paylink_qr_dark(int x, int y) { return qrcodegen_getModule(P.qr, x, y); }
