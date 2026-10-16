#include "ui_int.h"
#include "net.h"
#include "world.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { u32 guest, job_guest; int job, save; bool vip, staff, loading; char status[80]; } P;

void profile_open(u32 guest) {
  P.guest = guest; P.vip = P.staff = false; P.loading = true; P.status[0] = 0;
  char path[80]; snprintf(path, sizeof path, "/api/badges?guest=%u", (unsigned)guest);
  if (!P.job) { P.job_guest = guest; P.job = net_http("GET", path, NULL, false); }
  ui_open(P_PROFILE);
}

void profile_draw(u64 now) {
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Badges & identity")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  HttpResult result;
  if (P.job && net_http_result(P.job, &result)) {
    P.job = 0; P.loading = false;
    if (P.job_guest != P.guest) { free(result.body); profile_open(P.guest); return; }
    json_t *j = result.status == 200 && result.body ? json_loads(result.body, 0, NULL) : NULL;
    if (j) {
      json_t *array = json_object_get(j, "badges"), *code; size_t i;
      json_array_foreach(array, i, code) {
        const char *s = json_string_value(code);
        if (s) { P.vip |= !strcmp(s, "VIP"); P.staff |= !strcmp(s, "ADM"); }
      }
    } else snprintf(P.status, sizeof P.status, "Could not load badges. Try Refresh.");
    json_decref(j); free(result.body);
  }
  if (P.save && net_http_result(P.save, &result)) {
    P.save = 0;
    if (result.status == 200) profile_open(P.guest);
    snprintf(P.status, sizeof P.status, "%s", result.status == 200 ? "Identity updated." : "Could not update identity.");
    free(result.body);
  }
  float x = IN_X + 12, y = IN_Y + 10;
  if (P.vip) { sk_sprite("badge-VIP", x, y, 1); sk_text("VIP", x + 42, y + 10, 12, SK_TEXT, 0, 80); }
  if (P.staff) { sk_sprite("badge-ADM", x + 150, y, 1); sk_text("Staff", x + 190, y + 10, 12, SK_TEXT, 0, 80); }
  if (!P.vip && !P.staff) sk_text(P.loading ? "Loading badges…" : "No badges to display.", x, y + 10, 11, SK_DIM, 0, IN_W - 24);
  if (sk_button(x, y + 42, 110, "Refresh badges", !P.job)) profile_open(P.guest);
  json_t *owners = W.owners_json ? json_loads(W.owners_json, 0, NULL) : NULL;
  json_t *self = json_object_get(owners, "self");
  if (P.guest == W.guest_id && json_is_object(self)) {
    bool visible = json_is_true(json_object_get(self, "visible")), presence = json_is_true(json_object_get(self, "presence"));
    sk_text(visible ? "Founder identity visible" : "Anonymous appearance", x, y + 70, 11, SK_TEXT, 0, 270);
    bool toggle = sk_button(x, y + 92, 132, visible ? "Go anonymous" : "Show Founder", !P.save);
    bool aura = sk_button(x + 142, y + 92, 132, presence ? "Presence: on" : "Presence: off", !P.save && visible);
    if (toggle || aura) {
      char body[64]; snprintf(body, sizeof body, "{\"visible\":%s,\"presence\":%s}", (toggle ? !visible : visible) ? "true" : "false", (aura ? !presence : presence) ? "true" : "false");
      P.save = net_http("POST", "/api/owner-profile", body, true);
    }
  }
  json_decref(owners);
  sk_text(P.status, x, IN_Y + IN_H - 18, 11, SK_DIM, 0, IN_W - 24);
  (void)now;
}
