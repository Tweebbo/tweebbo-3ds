#include "language.h"
// Friends Console (social.ts): friends with their room, requests, search,
// ignored players and private messages, through /api/social.
#include <3ds.h>
#include <citro2d.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "net.h"
#include "ui_int.h"
#include "world.h"
#include "osk.h"

#define MAX_CONTACTS 64
#define MAX_MESSAGES 100

typedef struct { u32 id; char name[24], room[48], mission[204]; u64 look; int unread; } Contact;
typedef struct { u32 id, sender; char text[2004]; } Message;

static struct {
  Contact friends[MAX_CONTACTS], incoming[MAX_CONTACTS], outgoing[MAX_CONTACTS], blocked[MAX_CONTACTS], results[MAX_CONTACTS];
  int nf, ni, no, nb, nr;
  int view;  /* 0 friends, 1 requests, 2 search */
  u32 peer;  /* open conversation */
  Message msgs[MAX_MESSAGES];
  int nm, message_page, text_offset;
  int job, msg_job, act_job, search_job;
  u64 next_poll;
  bool loaded, push;
  ListState list, chat;
  char query[48], status[96], mission[204];
} S;

bool friends_is_blocked(u32 guest) {
  for (int i = 0; i < S.nb; i++) if (S.blocked[i].id == guest) return true;
  return false;
}

int friends_badge(void) {
  int n = S.ni;
  for (int i = 0; i < S.nf; i++) n += S.friends[i].unread;
  return n;
}

static int parse_contacts(json_t *arr, Contact *out) {
  int n = 0;
  size_t i;
  json_t *v;
  json_array_foreach(arr, i, v) {
    if (n >= MAX_CONTACTS) break;
    Contact *c = &out[n++];
    memset(c, 0, sizeof *c);
    c->id = (u32)json_integer_value(json_object_get(v, "id"));
    snprintf(c->mission,sizeof c->mission,"%s",json_string_value(json_object_get(v,"mission"))?:"");
    c->look = (u64)json_integer_value(json_object_get(v, "look"));
    c->unread = (int)json_integer_value(json_object_get(v, "unread"));
    snprintf(c->name, sizeof c->name, "%s", json_string_value(json_object_get(v, "name")) ?: "?");
    snprintf(c->room, sizeof c->room, "%s", json_string_value(json_object_get(v, "room")) ?: "");
  }
  return n;
}

static void load_messages(void);

/** Periodic polling like the web: fast while the console is open, slow in the background. */
void friends_poll(u64 now) {
  if (now == 0) { S.next_poll = 0; S.push = true; return; }
  if (!net_online() || !net_token()[0]) return;
  HttpResult r;
  if (S.job && net_http_result(S.job, &r)) {
    S.job = 0;
    json_error_t err;
    json_t *j = r.body && r.status == 200 ? json_loads(r.body, 0, &err) : NULL;
    if (j) {
      S.nf = parse_contacts(json_object_get(j, "friends"), S.friends);
      S.ni = parse_contacts(json_object_get(j, "incoming"), S.incoming);
      S.no = parse_contacts(json_object_get(j, "outgoing"), S.outgoing);
      S.nb = parse_contacts(json_object_get(j, "blocked"), S.blocked);
      snprintf(S.mission,sizeof S.mission,"%s",json_string_value(json_object_get(json_object_get(j,"self"),"mission"))?:"");
      S.loaded = true;
      json_decref(j);
    }
    free(r.body);
    if (S.peer && ui_panel() == P_FRIENDS) load_messages();
  }
  if (S.msg_job && net_http_result(S.msg_job, &r)) {
    S.msg_job = 0;
    json_error_t err;
    json_t *j = r.body && r.status == 200 ? json_loads(r.body, 0, &err) : NULL;
    if (j) {
      u32 selected=S.nm?S.msgs[S.nm-1-S.message_page].id:0;
      bool reading=S.message_page || S.text_offset;
      S.nm = 0;
      u32 last = 0;
      size_t i;
      json_t *v;
      json_array_foreach(j, i, v) {
        if (S.nm >= MAX_MESSAGES) break;
        Message *m = &S.msgs[S.nm++];
        m->id = (u32)json_integer_value(json_object_get(v, "id"));
        m->sender = (u32)json_integer_value(json_object_get(v, "sender"));
        snprintf(m->text, sizeof m->text, "%s", json_string_value(json_object_get(v, "text")) ?: "");
        last = m->id;
      }
      if(reading){for(int i=0;i<S.nm;i++)if(S.msgs[i].id==selected){S.message_page=S.nm-1-i;break;}}
      if(S.message_page>=S.nm){S.message_page=S.nm?S.nm-1:0;S.text_offset=0;}
      // Marks seen messages as read (affects the receiver only).
      if (last && S.peer) {
        char body[96];
        snprintf(body, sizeof body, "{\"action\":\"read\",\"peer\":%u,\"through\":%u}", (unsigned)S.peer, (unsigned)last);
        int job = net_http("POST", "/api/social", body, true);
        (void)job;
      }
      json_decref(j);
    }
    free(r.body);
  }
  if (S.act_job && net_http_result(S.act_job, &r)) {
    S.act_job = 0;
    if (r.status != 200) {
      json_error_t err;
      json_t *j = r.body ? json_loads(r.body, 0, &err) : NULL;
      const char *msg = j ? json_string_value(json_object_get(j, "error")) : NULL;
      ui_toast(msg ? msg : r.err[0] ? r.err : TR("Could not connect to the messenger."), now);
      if (j) json_decref(j);
    }
    free(r.body);
    S.next_poll = 0;
  }
  if (S.search_job && net_http_result(S.search_job, &r)) {
    S.search_job = 0;
    json_error_t err;
    json_t *j = r.body && r.status == 200 ? json_loads(r.body, 0, &err) : NULL;
    S.nr = j ? parse_contacts(j, S.results) : 0;
    snprintf(S.status, sizeof S.status, "%s", S.nr ? "" : "Nobody found with that name.");
    if (j) json_decref(j);
    free(r.body);
  }
  bool open = ui_panel() == P_FRIENDS;
  if (!S.job && now >= S.next_poll) {
    S.job = net_http("GET", "/api/social", NULL, true);
    S.next_poll = now + (open ? 3000 : S.push ? 300000 : 30000);
  }
}

void friends_opened(void) {
  S.next_poll = 0;
  S.list.focus = -1;
}

void friends_act(const char *action, u32 peer, const char *text) {
  if (!net_online()) { ui_toast(TR("Connect to the hotel to use the Friends Console."), now_ms()); return; }
  if(S.act_job)return;
  json_t *j=json_pack("{s:s,s:I,s:s}","action",action,"peer",(json_int_t)peer,"text",text?text:"");
  char *body=j?json_dumps(j,JSON_COMPACT):NULL;json_decref(j);
  S.act_job=body?net_http("POST","/api/social",body,true):0;free(body);
  if(!S.act_job)ui_toast(TR("Could not send. Please try again."),now_ms());
}

static void load_messages(void) {
  if (S.msg_job || !S.peer) return;
  char path[64];
  snprintf(path, sizeof path, "/api/social?peer=%u", (unsigned)S.peer);
  S.msg_job = net_http("GET", path, NULL, true);
}

static Contact *find(Contact *list, int n, u32 id) {
  for (int i = 0; i < n; i++) if (list[i].id == id) return &list[i];
  return NULL;
}

static void search_done(const char *q, void *ctx) {
  (void)ctx;
  snprintf(S.query, sizeof S.query, "%s", q);
  char esc[160], path[200];
  int o = 0;
  for (const char *p = q; *p && o < (int)sizeof esc - 4; p++) {
    unsigned char ch = (unsigned char)*p;
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') esc[o++] = (char)ch;
    else o += snprintf(esc + o, sizeof esc - (size_t)o, "%%%02X", ch);
  }
  esc[o] = 0;
  snprintf(path, sizeof path, "/api/social?q=%s", esc);
  S.search_job = net_http("GET", path, NULL, true);
  snprintf(S.status, sizeof S.status,"%s",TR("Searching…"));
}

static void message_done(const char *text, void *ctx) {
  (void)ctx;
  friends_act("message", S.peer, text);
  S.next_poll = now_ms() + 400;
}

static void conversation(u64 now) {
  Contact *c = find(S.friends, S.nf, S.peer);
  if (!c) { S.peer = 0; return; }
  float x = IN_X + 4, w = IN_W - 8;
  sk_head(c->look, x + 10, IN_Y + 12);
  sk_text(c->name, x + 26, IN_Y + 3, 11.5f, SK_TEXT, 0, 150);
  sk_text(c->room[0] ? TR("Online") : TR("Offline"), x + 26, IN_Y + 16, 9, c->room[0] ? SK_GO : SK_DIM, 0, 100);
  if (sk_button(x + w - 50, IN_Y + 3, 50, TR("Back"), true)) { S.peer = 0; return; }
  if (c->room[0] && sk_button(x + w - 128, IN_Y + 3, 74, TR("Visit room"), strcmp(c->room, public_slug(W.slug)) != 0)) {
    net_goto(c->room, "");
    ui_close();
    return;
  }
  sk_paragraph(c->mission,IN_X+6,IN_Y+29,IN_W-12,9,SK_DIM,2);
  Message *m=S.nm?&S.msgs[S.nm-1-S.message_page]:NULL;
  sk_text(m?(m->sender==W.guest_id?W.name:c->name):S.msg_job?TR("Loading…"):TR("No messages yet."),IN_X+6,IN_Y+54,10,SK_TEXT,0,IN_W-12);
  if(m && S.text_offset>(int)strlen(m->text))S.text_offset=0;
  int next=m?sk_paragraph_page(m->text,S.text_offset,IN_X+6,IN_Y+70,IN_W-12,10,SK_TEXT,3):0;
  float by=IN_Y+IN_H-44;
  if(S.text_offset && (sk_button(IN_X+4,by,110,TR("Start (Up)"),true)||K.up))S.text_offset=0;
  if(m && next<(int)strlen(m->text) && (sk_button(IN_X+IN_W-114,by,110,TR("More (Down)"),true)||K.dn))S.text_offset=next;
  by=IN_Y+IN_H-21;
  if(sk_button(IN_X+4,by,90,TR("Older"),S.message_page+1<S.nm)||K.left){if(S.message_page+1<S.nm){S.message_page++;S.text_offset=0;}}
  if(sk_button(IN_X+98,by,90,TR("Newer"),S.message_page>0)||K.right){if(S.message_page>0){S.message_page--;S.text_offset=0;}}
  if(sk_button(IN_X+IN_W-96,by,92,TR("Write (A)"),!S.act_job)||(K.a&&!S.act_job))osk_open(TR("Message"),NULL,500,message_done,NULL);
  static u64 refreshed;
  if (now - refreshed > 3000) { refreshed = now; load_messages(); }
}

static void mission_done(const char *text,void *ctx) {
  (void)ctx;if(S.act_job)return;
  json_t *j=json_pack("{s:s,s:i,s:s}","action","mission","peer",0,"text",text);
  char *body=j?json_dumps(j,JSON_COMPACT):NULL;json_decref(j);
  S.act_job=body?net_http("POST","/api/social",body,true):0;free(body);
  if(!S.act_job)ui_toast(TR("Could not save the mission."),now_ms());
}
void friends_draw(u64 now) {
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, TR("Friends Console"))) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  if (!net_online()) {
    sk_paragraph(net_state() == NET_OFFLINE ? TR("The Friends Console works online. Open Help to connect to tweebbo.lol.")
                                             : TR("Connecting to the hotel…"),
                 IN_X + 8, IN_Y + 10, IN_W - 16, 11, SK_TEXT, 4);
    return;
  }
  if (S.peer) { conversation(now); return; }
  char tabs[4][32];
  snprintf(tabs[0], sizeof tabs[0], TR("Friends (%d)"), S.nf);
  snprintf(tabs[1], sizeof tabs[1], S.ni ? TR("Requests (%d)") : TR("Requests"), S.ni);
  snprintf(tabs[2], sizeof tabs[2],"%s",TR("Search"));
  snprintf(tabs[3],sizeof tabs[3],"%s",TR("My info"));
  float tw = (IN_W - 10) / 4;
  for (int i = 0; i < 4; i++)
    if (sk_tab(IN_X + 2 + i * (tw + 2), IN_Y + 2, tw, 16, tabs[i], S.view == i)) { S.view = i; S.list.scroll = 0; S.list.focus = -1; }
  if (K.left || K.right) { S.view = (S.view + (K.right ? 1 : 3)) % 4; S.list.focus = -1; }
  float ly = IN_Y + 22, lh = IN_H - 24;
  if(S.view==3) {
    sk_text(W.name,IN_X+8,ly+6,11,SK_TEXT,0,IN_W-16);
    sk_text(TR("Mission"),IN_X+8,ly+27,10,SK_DIM,0,IN_W-16);
    sk_paragraph(S.mission,IN_X+8,ly+45,IN_W-16,11,SK_TEXT,4);
    if(sk_button(IN_X+8,IN_Y+IN_H-27,IN_W-16,TR("Edit mission (A)"),!S.act_job) || (K.a&&!S.act_job))osk_open(TR("Mission (up to 50 characters)"),S.mission,50,mission_done,NULL);
    return;
  }
  const float row = 24;
  if (S.view == 2) {
    if (sk_field(IN_X + 4, ly, IN_W - 72, S.query, TR("Player name")) || sk_button(IN_X + IN_W - 64, ly, 60, TR("Search"), true) || K.x) {
      osk_open(TR("Search for a player"), S.query, 40, search_done, NULL);
    }
    ly += 22; lh -= 22;
  }
  // Rows for the current view.
  Contact *list = S.view == 0 ? S.friends : S.view == 1 ? S.incoming : S.results;
  int n = S.view == 0 ? S.nf : S.view == 1 ? S.ni + S.no : S.nr;
  fill_rect(IN_X + 2, ly, IN_W - 4, lh, sk_rgb(SK_WHITE));
  if (K.up) S.list.focus = S.list.focus <= 0 ? 0 : S.list.focus - 1;
  if (K.dn) S.list.focus = S.list.focus + 1 >= n ? n - 1 : S.list.focus + 1;
  int first = sk_list_begin(&S.list, IN_X + 2, ly, IN_W - 4, lh, n, row);
  for (int i = first; i < n; i++) {
    float y = sk_list_row_y(i);
    if (y > ly + lh) break;
    bool outgoing = S.view == 1 && i >= S.ni;
    Contact *c = outgoing ? &S.outgoing[i - S.ni] : &list[i];
    bool focus = S.list.focus == i;
    fill_rect(IN_X + 2, y, IN_W - 4, row - 1, sk_rgb(focus ? SK_SELECT : i % 2 ? SK_ROW_ALT : SK_ROW));
    sk_head(c->look, IN_X + 14, y + 12);
    sk_text(c->name, IN_X + 28, y + 2, 10.5f, SK_TEXT, 0, 120);
    float bx = IN_X + IN_W - 6;
    if (S.view == 0) {
      sk_text(c->room[0] ? TR("Online") : TR("Offline"), IN_X + 28, y + 13, 8.5f, c->room[0] ? SK_GO : SK_DIM, 0, 80);
      if (c->unread) {
        char u[8];
        snprintf(u, sizeof u, "%d", c->unread);
        C2D_DrawCircleSolid(IN_X + 156, y + 12, 0.5f, 7, sk_rgb(0xd33a2c));
        text_draw(u, IN_X + 156, y + 6, 8.5f, sk_rgb(SK_WHITE), 1);
      }
      if (sk_go(bx - 46, y + 3, 44, 17, TR("Chat"), true) || (focus && K.a)) { S.peer = c->id; S.nm = 0; S.message_page=0;S.text_offset=0;load_messages(); }
      if (c->room[0] && sk_go(bx - 94, y + 3, 44, 17, TR("Visit"), true)) { net_goto(c->room, ""); ui_close(); return; }
      if (sk_button(bx - 150, y + 3, 52, TR("Remove"), true)) friends_act("remove", c->id, "");
    } else if (S.view == 1 && !outgoing) {
      sk_text(TR("wants to be your friend"), IN_X + 28, y + 13, 8.5f, SK_DIM, 0, 120);
      if (sk_go(bx - 52, y + 3, 50, 17, TR("Accept"), true) || (focus && K.a)) friends_act("accept", c->id, "");
      if (sk_button(bx - 110, y + 3, 54, TR("Decline"), true)) friends_act("reject", c->id, "");
    } else if (outgoing) {
      sk_text(TR("request sent"), IN_X + 28, y + 13, 8.5f, SK_DIM, 0, 120);
      if (sk_button(bx - 60, y + 3, 58, TR("Cancel"), true)) friends_act("cancel", c->id, "");
    } else {
      bool friend_ = find(S.friends, S.nf, c->id) != NULL, pending = find(S.outgoing, S.no, c->id) != NULL;
      if (c->id == W.guest_id) sk_text(TR("You"), bx - 4, y + 6, 9.5f, SK_DIM, 2, 60);
      else if (friend_) sk_text(TR("Friend"), bx - 4, y + 6, 9.5f, SK_DIM, 2, 60);
      else if (pending) sk_text(TR("Request sent"), bx - 4, y + 6, 9.5f, SK_DIM, 2, 80);
      else if (sk_go(bx - 74, y + 3, 72, 17, TR("Add friend"), true) || (focus && K.a)) friends_act("request", c->id, "");
    }
  }
  sk_list_end();
  if (!n) {
    const char *empty = S.view == 0 ? (S.loaded ? "No friends yet. Select someone in a room to add them." : "Loading…")
                      : S.view == 1 ? "No friend requests." : S.status[0] ? S.status : "Search for players by name.";
    sk_paragraph(empty, IN_X + 10, ly + 14, IN_W - 20, 10.5f, SK_DIM, 3);
  }
}
