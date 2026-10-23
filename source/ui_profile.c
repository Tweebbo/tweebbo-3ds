#include "ui_int.h"
#include "net.h"
#include "world.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { u32 guest, job_guest; int job, save; bool vip, staff, club, gold, loading, picking; int count, choice; char codes[8][16]; char status[80]; } P;

void profile_open(u32 guest) {
  P.guest = guest; P.vip = P.staff = P.club = P.gold = false; P.loading = true; P.picking=false; P.count=P.choice=0; P.status[0] = 0;
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
        if (s) { if(P.count<8)snprintf(P.codes[P.count++],16,"%s",s);P.vip |= !strcmp(s, "VIP"); P.staff |= !strcmp(s, "ADM"); P.club |= !strcmp(s, "CLUB1"); P.gold |= !strcmp(s, "CLUB2"); }
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
  if(P.picking&&P.guest==W.guest_id){
    sk_text("Choose your profile badge",x,y,12,SK_TEXT,0,IN_W-24);
    for(int k=0;k<P.count;k++){char label[40];snprintf(label,sizeof label,"%s%s",P.choice==k?"> ":"",P.codes[k]);if(sk_button(x+(k%2)*138,y+25+(k/2)*24,132,label,!P.save))P.choice=k;}
    if(sk_button(x,y+128,110,"Back",!P.save))P.picking=false;
    if(sk_button(x+120,y+128,150,"Show this badge",!P.save&&P.count)){json_t *j=json_pack("{s:s}","code",P.codes[P.choice]);char *body=json_dumps(j,JSON_COMPACT);json_decref(j);P.save=net_http("POST","/api/badges",body,true);free(body);}
    sk_text(P.status,x,IN_Y+IN_H-18,10,SK_DIM,0,IN_W-24);return;
  }
  // Badges flow left to right: VIP, Staff, then the Tweebbo Club badge (gold for life members).
  struct { bool on; const char *sprite, *label; } badges[] = {
    {P.vip, "badge-VIP", "VIP"}, {P.staff, "badge-ADM", "Staff"},
    {P.gold, "badge-CLUB2", "Club Gold"}, {P.club && !P.gold, "badge-CLUB1", "Club"},
  };
  float bx = x;
  for (unsigned i = 0; i < sizeof badges / sizeof badges[0]; i++) {
    if (!badges[i].on) continue;
    sk_sprite(badges[i].sprite, bx, y, 1);
    sk_text(badges[i].label, bx + 42, y + 10, 11, SK_TEXT, 0, 56);
    bx += 100;
  }
  if (!P.vip && !P.staff && !P.club && !P.gold) sk_text(P.loading ? "Loading badges…" : "No badges to display.", x, y + 10, 11, SK_DIM, 0, IN_W - 24);
  if (sk_button(x, y + 42, 110, "Refresh badges", !P.job)) profile_open(P.guest);
  if(P.guest==W.guest_id&&sk_button(x+120,y+42,150,"Choose badge",!P.job&&!P.save&&P.count))P.picking=true;
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
