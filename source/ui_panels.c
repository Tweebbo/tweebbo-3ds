#include "language.h"
#include "gen/game_rooms.h"
// Web edition windows ported to the bottom screen: room navigator
// (with creation), catalogue, hand (inventory), furniture panel and pets.
#include <3ds.h>
#include "osk.h"
#include <citro2d.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "proto.h"
#include "scene.h"
#include "ui_int.h"
#include "world.h"

static bool online_or_toast(void) {
  if (net_state() == NET_OFFLINE) {
    ui_toast(TR("Offline practice: connect to tweebbo.lol in Help."), now_ms());
    return false;
  }
  return true;
}

// ================================================================ navigator

#define MAX_ROOMS 60
#define MAX_MODELS 16

// Titles hold 40 characters of up to 4 UTF-8 bytes each (the server's limit).
typedef struct { char slug[48], title[164], owner[64], activity[64]; int online, priority; bool pub, mine, locked, featured, credits, bar, igloo; } RoomRow;

static int room_order(const void *a,const void *b) {
  const RoomRow *x=a,*y=b;
  if(x->priority!=y->priority)return x->priority<y->priority?-1:1;
  return x->online==y->online?strcmp(x->slug,y->slug):x->online>y->online?-1:1;
}

static struct {
  int tab; /* 0 all, 1 public, 2 mine */
  int job, create_job, lock_job, rename_job, delete_job;
  RoomRow rooms[MAX_ROOMS];
  int n;
  struct { char key[24], name[48]; } models[MAX_MODELS];
  int model_n, model;
  bool loaded, creating;
  char error[96], new_title[164], new_password[65], lock_slug[48];
  u64 refreshed;
  ListState list;
} NV;

static const char *SCOPES[] = {"all", "public", "mine"};

// Public rooms packed into the offline simulation (tools/pack.py ROOMS).
static const struct { const char *slug, *title; } OFFLINE_ROOMS[] = {
  {"lobby", "Classic Lobby"},
  {"park", "Classic Park"},
  {"pool", "The Pool"},
  {"diving", "Diving Deck"},
  {"rooftop", "Rooftop Rumble"},
  {"casino", "Casino"},
  {"afterhours", "After Hours Bar"},
  {"igloo", "Igloo"},
  {"lobby-es", "Spanish Lobby"},
  {"lobby-zh", "Chinese Lobby"},
  {"lobby-ja", "Japanese Lobby"},
};

static void offline_rows(void) {
  NV.n = 0; NV.error[0] = 0;
  for (int i = 0; i < (int)COUNT(OFFLINE_ROOMS) && NV.n < MAX_ROOMS; i++) {
    RoomRow *row = &NV.rooms[NV.n++];
    snprintf(row->slug, sizeof row->slug, "%s", OFFLINE_ROOMS[i].slug);
    utf8_copy(row->title, sizeof row->title, OFFLINE_ROOMS[i].title);
    row->pub = true;
    row->mine=false;row->locked=false;row->credits=!strcmp(row->slug,"casino");row->bar=!strcmp(row->slug,"afterhours");row->igloo=!strcmp(row->slug,"igloo");row->featured=row->credits||row->bar||!strcmp(row->slug,tw_reception_slug());
    row->online=0;row->priority=!strcmp(row->slug,tw_reception_slug())?10:row->credits?20:row->bar?25:2147483647;
    utf8_copy(row->activity,sizeof row->activity,row->credits?"Casino games":row->bar?"Drinks and smokes":row->featured?"Welcome":"");
  }
  for (const GameRoomPort *p = game_room_ports; p->model && NV.n < MAX_ROOMS; p++) {
    for (int i = 0; i < room_model_count; i++) {
      if (strcmp(room_models[i].key, p->model)) continue;
      RoomRow *row = &NV.rooms[NV.n++];
      memset(row, 0, sizeof *row);
      snprintf(row->slug, sizeof row->slug, "%s", p->slug);
      utf8_copy(row->title, sizeof row->title, room_models[i].name);
      row->pub = true;
      row->priority = 2147483647;
      break;
    }
  }
  qsort(NV.rooms,NV.n,sizeof NV.rooms[0],room_order);
  NV.loaded = true;
}

static void nav_refresh(void) {
  if (NV.job || net_state() == NET_OFFLINE) return;
  char path[48];
  snprintf(path, sizeof path, "/api/rooms?scope=%s", SCOPES[NV.tab]);
  NV.job = net_http("GET", path, NULL, true);
  NV.refreshed = now_ms();
}

void nav_opened(void) {
  NV.creating = false;
  NV.list.focus = -1;
  if (net_state() == NET_OFFLINE) { offline_rows(); return; }
  nav_refresh();
}

static void nav_poll(void) {
  HttpResult r;
  if (NV.job && net_http_result(NV.job, &r)) {
    NV.job = 0;
    json_error_t err;
    json_t *j = r.body ? json_loads(r.body, 0, &err) : NULL;
    if (r.status == 200 && j) {
      NV.n = 0;
      NV.error[0] = 0;
      size_t i;
      json_t *v;
      json_array_foreach(json_object_get(j, "rooms"), i, v) {
        if (NV.n >= MAX_ROOMS) break;
        const char *lang = json_string_value(json_object_get(json_object_get(v, "featured"), "lang"));
        RoomRow *row = &NV.rooms[NV.n++];
        snprintf(row->slug, sizeof row->slug, "%s", json_string_value(json_object_get(v, "slug")) ?: "");
        utf8_copy(row->title, sizeof row->title, json_string_value(json_object_get(v, "title")));
        utf8_copy(row->owner, sizeof row->owner, json_string_value(json_object_get(v, "owner")));
        row->online = (int)json_integer_value(json_object_get(v, "online"));
        row->pub = json_is_true(json_object_get(v, "public"));
        row->mine = json_is_true(json_object_get(v, "mine"));
        row->locked = json_is_true(json_object_get(v, "locked"));
        json_t *featured = json_object_get(v, "featured");
        row->featured = row->pub && json_integer_value(json_object_get(v,"ownerId"))==0 && json_is_object(featured) && (!lang || !strcmp(lang,tw_reception_language()));
        row->priority = row->featured ? (int)json_integer_value(json_object_get(featured,"order")) : 2147483647;
        const char *accent = json_string_value(json_object_get(featured, "accent"));
        row->credits = row->featured && accent && !strcmp(accent, "credits");
        const char *icon = json_string_value(json_object_get(featured, "icon"));
        row->bar = row->featured && icon && !strcmp(icon, "bar_cigar_icon");
        row->igloo = row->featured && icon && !strcmp(icon, "igloo_icon");
        utf8_copy(row->activity, sizeof row->activity, row->featured ? json_string_value(json_object_get(featured, "label")) : NULL);
      }
      qsort(NV.rooms,NV.n,sizeof NV.rooms[0],room_order);
      NV.model_n = 0;
      json_array_foreach(json_object_get(j, "models"), i, v) {
        if (NV.model_n >= MAX_MODELS) break;
        snprintf(NV.models[NV.model_n].key, sizeof NV.models[0].key, "%s", json_string_value(json_object_get(v, "key")) ?: "");
        snprintf(NV.models[NV.model_n].name, sizeof NV.models[0].name, "%s", json_string_value(json_object_get(v, "name")) ?: "");
        NV.model_n++;
      }
      NV.loaded = true;
    } else {
      const char *msg = j ? json_string_value(json_object_get(j, "error")) : NULL;
      snprintf(NV.error, sizeof NV.error, "%s", msg ? msg : r.err[0] ? r.err : "Could not load the rooms.");
    }
    if (j) json_decref(j);
    free(r.body);
  }
  if (NV.rename_job && net_http_result(NV.rename_job, &r)) {
    NV.rename_job=0;
    json_t *j=r.body?json_loads(r.body,0,NULL):NULL;
    const char *message=j?json_string_value(json_object_get(j,"error")):NULL;
    ui_toast(r.status==200?TR("Room name saved."):message?message:TR("Could not rename the room."),now_ms());
    if(r.status==200)nav_refresh();
    json_decref(j);free(r.body);
  }
  if (NV.delete_job && net_http_result(NV.delete_job, &r)) {
    NV.delete_job=0;
    json_t *j=r.body?json_loads(r.body,0,NULL):NULL;
    const char *message=j?json_string_value(json_object_get(j,"error")):NULL;
    ui_toast(r.status==200?TR("Room deleted."):message?message:TR("Could not delete the room."),now_ms());
    if(r.status==200)nav_refresh();
    json_decref(j);free(r.body);
  }
  if (NV.lock_job && net_http_result(NV.lock_job, &r)) {
    NV.lock_job = 0;
    json_t *j = r.body ? json_loads(r.body, 0, NULL) : NULL;
    if (r.status == 200 && j) {
      ui_toast(json_is_true(json_object_get(j, "locked")) ? TR("Room locked: only people with the password can enter.") : TR("Room open to everybody."), now_ms());
      nav_refresh();
    } else {
      const char *msg = j ? json_string_value(json_object_get(j, "error")) : NULL;
      ui_toast(msg ? msg : r.err[0] ? r.err : TR("Could not change the password."), now_ms());
    }
    if (j) json_decref(j);
    free(r.body);
  }
  if (NV.create_job && net_http_result(NV.create_job, &r)) {
    NV.create_job = 0;
    json_error_t err;
    json_t *j = r.body ? json_loads(r.body, 0, &err) : NULL;
    const char *slug = j ? json_string_value(json_object_get(j, "slug")) : NULL;
    dbg_log("Create room: HTTP %d%s", r.status, slug ? "" : " without a room");
    if (r.status == 200 && slug) {
      ui_toast(TR("Room created. Taking you there…"), now_ms());
      NV.creating = false;
      NV.new_title[0] = 0;
      memset(NV.new_password, 0, sizeof NV.new_password);
      net_goto(slug, "");
      ui_close();
    } else {
      const char *msg = j ? json_string_value(json_object_get(j, "error")) : NULL;
      ui_toast(msg ? msg : r.err[0] ? r.err : TR("Could not create the room."), now_ms());
    }
    if (j) json_decref(j);
    free(r.body);
  }
}

static void nav_create(void) {
  if (NV.create_job || !NV.model_n) return;
  char title[400];
  int len = (int)strlen(NV.new_title);
  if (len < 3) { ui_toast(TR("Room names need 3 to 40 characters."), now_ms()); return; }
  json_escape(NV.new_title, title, sizeof title);
  char token[200], password[140], body[1000];
  json_escape(net_token(), token, sizeof token);
  json_escape(NV.new_password, password, sizeof password);
  snprintf(body, sizeof body, "{\"title\":\"%s\",\"model\":\"%s\",\"token\":\"%s\",\"password\":\"%s\"}", title,
           NV.models[NV.model].key, token, password);
  memset(password, 0, sizeof password);
  dbg_log("Create room: %d-byte name, model %s", len, NV.models[NV.model].key);
  NV.create_job = net_http("POST", "/api/rooms", body, false);
}

static char lock_slug_pending[48];

static void nav_lock_send(const char *password) {
  char slug[100], secret[140], body[300];
  json_escape(lock_slug_pending, slug, sizeof slug);
  json_escape(password, secret, sizeof secret);
  snprintf(body, sizeof body, "{\"slug\":\"%s\",\"password\":\"%s\"}", slug, secret);
  NV.lock_job = net_http("POST", "/api/rooms/lock", body, true);
  utf8_copy(NV.lock_slug, sizeof NV.lock_slug, lock_slug_pending);
  memset(secret, 0, sizeof secret); memset(body, 0, sizeof body);
}

static void nav_lock_done(const char *password, void *ctx) {
  (void)ctx;
  if (strlen(password) < 4) { ui_toast(TR("Use a password of 4-32 characters."), now_ms()); return; }
  nav_lock_send(password);
}

static void nav_lock(const RoomRow *r) {
  utf8_copy(lock_slug_pending, sizeof lock_slug_pending, r->slug);
  // Locked: open it again. Open: ask for the new password on the touch keyboard.
  if (r->locked) nav_lock_send("");
  else osk_open_secret(TR("New room password (4-32 characters)"), 32, nav_lock_done, NULL);
}

static char rename_slug_pending[48];
static char delete_slug_pending[48];
static void nav_delete_confirmed(void *ctx) {
  (void)ctx;
  json_t *j=json_pack("{s:s}","slug",delete_slug_pending);
  char *body=json_dumps(j,JSON_COMPACT);json_decref(j);
  if(body){NV.delete_job=net_http("POST","/api/rooms/delete",body,true);free(body);}
}
static void nav_delete(const RoomRow *r) {
  utf8_copy(delete_slug_pending,sizeof delete_slug_pending,r->slug);
  ui_confirm(TR("Delete this room? Everyone leaves. Items and pets return to their owners."),TR("Delete"),nav_delete_confirmed,NULL);
}
static void nav_rename_done(const char *text, void *ctx) {
  (void)ctx;
  if(NV.rename_job)return;
  json_t *j=json_pack("{s:s,s:s}","slug",rename_slug_pending,"title",text);
  char *body=j?json_dumps(j,JSON_COMPACT):NULL;json_decref(j);
  NV.rename_job=body?net_http("POST","/api/rooms/rename",body,true):0;
  free(body);
  if(!NV.rename_job)ui_toast(TR("Could not rename the room."),now_ms());
}
static void nav_rename(const RoomRow *r) {
  utf8_copy(rename_slug_pending,sizeof rename_slug_pending,r->slug);
  osk_open(TR("New room name (3-40 characters)"),r->title,40,nav_rename_done,NULL);
}

static void room_name_done(const char *text, void *ctx) { (void)ctx; utf8_copy(NV.new_title, sizeof NV.new_title, text); }
static void room_password_done(const char *text, void *ctx) {
  (void)ctx;
  if (strlen(text) < 4) { ui_toast(TR("Use a password of 4-32 characters."), now_ms()); return; }
  utf8_copy(NV.new_password, sizeof NV.new_password, text);
}

static void nav_create_view(void) {
  float x = IN_X + 6, y = IN_Y + 4, w = IN_W - 12;
  sk_text(TR("Create a room"), x, y, 12, SK_TEXT, 0, w);
  text_draw(TR("Create a room"), x + 0.6f, y, 12, sk_rgb(SK_TEXT), 0);
  y += 18;
  sk_text(TR("Room name"), x, y - 1, 9, SK_DIM, 0, w);
  if (sk_field(x, y + 11, w, NV.new_title, TR("Tap to type a name")))
    osk_open(TR("Room name (3-40 characters)"), NV.new_title, 40, room_name_done, NULL);
  y += 34;
  sk_text(TR("Layout"), x, y - 1, 9, SK_DIM, 0, w);
  if (NV.model_n) {
    if (sk_button(x, y + 11, 24, "<", true) || K.left) NV.model = (NV.model + NV.model_n - 1) % NV.model_n;
    if (sk_button(x + w - 24, y + 11, 24, ">", true) || K.right) NV.model = (NV.model + 1) % NV.model_n;
    fill_round(x + 28, y + 11, w - 56, 18, 3, sk_rgb(SK_WHITE));
    sk_text(NV.models[NV.model].name, x + w / 2, y + 13, 10.5f, SK_TEXT, 1, w - 64);
  } else sk_text(TR("Loading layouts…"), x, y + 13, 10, SK_DIM, 0, w);
  y += 34;
  // Door: open, or a password (4-32 characters) like the web's "Password (optional)".
  sk_text(TR("Door"), x, y - 1, 9, SK_DIM, 0, w);
  bool locked = NV.new_password[0] != 0;
  sk_sprite(locked ? "door_password@18" : "building_icon@18", x, y + 11, 1);
  if (sk_button(x + 22, y + 11, w - 22 - (locked ? 72 : 0), locked ? TR("Locked with a password (Y)") : TR("Open to everyone · add a password (Y)"), true) || K.y)
    osk_open_secret(TR("Room password (4-32 characters)"), 32, room_password_done, NULL);
  if (locked && sk_button(x + w - 68, y + 11, 68, TR("Remove"), true)) memset(NV.new_password, 0, sizeof NV.new_password);
  y += 30;
  sk_text(locked ? TR("Only people who know the password can enter.") : TR("Create your own rooms. Only you can decorate them."), x, y, 8.5f, SK_DIM, 0, w);
  float by = IN_Y + IN_H - 24, bw = (w - 8) / 2;
  if (sk_button(x, by, bw, TR("Back"), true)) NV.creating = false;
  if (sk_button_big(x + bw + 8, by - 3, bw, NV.create_job ? TR("Creating…") : TR("Create room (A)"),
                    !NV.create_job && strlen(NV.new_title) >= 3 && NV.model_n) || (K.a && strlen(NV.new_title) >= 3))
    nav_create();
}

void nav_draw(u64 now) {
  nav_poll();
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, TR("Navigator"))) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  bool offline = net_state() == NET_OFFLINE;
  if (offline && !NV.n) offline_rows();
  if (!offline && NV.creating) { nav_create_view(); return; }
  if (!offline) {
    static const char *TABS[] = {"All Rooms", "Public Rooms", "My Rooms"};
    float tw = (IN_W - 8) / 3;
    for (int i = 0; i < 3; i++)
      if (sk_tab(IN_X + 2 + i * (tw + 2), IN_Y + 2, tw, 16, TR(TABS[i]), NV.tab == i) && NV.tab != i) {
        NV.tab = i; NV.n = 0; NV.loaded = false; NV.list.scroll = 0; nav_refresh();
      }
    if (K.left || K.right) { NV.tab = (NV.tab + (K.right ? 1 : 2)) % 3; NV.n = 0; NV.loaded = false; nav_refresh(); }
    // An open navigator refreshes every 15 s, like the web.
    if (now - NV.refreshed > 15000) nav_refresh();
  } else {
    sk_text(TR("Offline practice · public rooms with bots"), IN_X + 4, IN_Y + 4, 10.5f, SK_DIM, 0, IN_W - 8);
  }
  const float row = 26;
  // Whole rows only: a half-clipped last row made its icon look broken.
  float ly = IN_Y + 22, lh = (float)((int)((IN_H - 48) / row)) * row;
  fill_rect(IN_X + 2, ly, IN_W - 4, IN_H - 48, sk_rgb(SK_WHITE));
  if (K.up) NV.list.focus = NV.list.focus <= 0 ? 0 : NV.list.focus - 1;
  if (K.dn) NV.list.focus = NV.list.focus + 1 >= NV.n ? NV.n - 1 : NV.list.focus + 1;
  int first = sk_list_begin(&NV.list, IN_X + 2, ly, IN_W - 4, lh, NV.n, row);
  for (int i = first; i < NV.n; i++) {
    float y = sk_list_row_y(i);
    if (y > ly + lh) break;
    RoomRow *r = &NV.rooms[i];
    bool focus = NV.list.focus == i;
    u32 background = r->credits ? (focus ? SK_CREDITS_SELECT : SK_CREDITS) : focus ? SK_SELECT : r->featured ? SK_FEATURED : i % 2 ? SK_ROW_ALT : SK_ROW;
    fill_rect(IN_X + 2, y, IN_W - 4, row - 1, sk_rgb(background));
    fill_rect(IN_X + 2, y + row - 1, IN_W - 4, 1, sk_rgb(SK_LINE));
    if (r->credits && !r->locked) sk_icon_coin(IN_X + 8, y + 5);
    else if (r->bar && !r->locked) sk_sprite("bar_cigar_icon@18", IN_X + 5, y + 4, 1);
    else if (r->igloo && !r->locked) sk_sprite("igloo_icon@18", IN_X + 5, y + 4, 1);
    else sk_sprite(r->locked ? "door_password@18" : "building_icon@18", IN_X + 5, y + 4, 1);
    sk_text(r->pub?TR(r->title):r->title, IN_X + 28, y + 2, 10.5f, r->credits ? SK_CREDITS_INK : SK_TEXT, 0, r->mine && NV.tab==2?IN_W-180:IN_W-120);
    char sub[96];
    if (r->featured) snprintf(sub, sizeof sub, TR("Featured · %s"), TR(r->activity));
    else if (r->pub) snprintf(sub, sizeof sub,"%s",TR("Public room"));
    else snprintf(sub, sizeof sub, r->locked ? TR("by %s · locked") : TR("by %s"), r->owner);
    sk_text(sub, IN_X + 28, y + 13, 9, SK_DIM, 0, r->mine && NV.tab==2?IN_W-180:IN_W-120);
    bool here = !strcmp(public_slug(W.slug), r->slug);
    if (r->mine && NV.tab == 2) {
      // Owners lock their room with a password or open it again, as in the web's My Rooms.
      bool busy = NV.lock_job != 0 || NV.rename_job != 0 || NV.delete_job != 0;
      if(sk_go(IN_X + IN_W - 145,y+4,48,18,TR("Rename"),!busy) || (focus && K.y && !busy))nav_rename(r);
      if (sk_go(IN_X + IN_W - 94, y + 4, 46, 18, busy && !strcmp(NV.lock_slug, r->slug) ? "…" : r->locked ? TR("Open") : TR("Lock"), !busy))
        nav_lock(r);
    } else {
      char inside[16];
      snprintf(inside, sizeof inside, TR("%d inside"), r->online);
      sk_text(inside, IN_X + IN_W - 50, y + 7, 9, SK_DIM, 2, 60);
    }
    if (sk_go(IN_X + IN_W - 44, y + 4, 34, 18, here ? TR("Here") : TR("Go"), offline || r->online > 0) || (focus && K.a)) {
      // A locked room asks for its password on the status screen (owners enter without one).
      if (!here) { net_goto(r->slug, ""); ui_close(); }
    }
  }
  sk_list_end();
  if (!NV.n) sk_text(NV.error[0] ? NV.error : NV.job ? TR("Loading rooms…") : offline ? TR("No rooms packed for offline play.") : NV.tab == 2 ? TR("You have not created any rooms yet.") : TR("No rooms."),
                     BOT_W / 2, ly + 30, 10.5f, NV.error[0] ? SK_DANGER : SK_DIM, 1, IN_W - 20);
  float by = IN_Y + IN_H - 22;
  if (offline) {
    sk_text(TR("Help changes the server or returns online."), IN_X + 4, by + 3, 9.5f, SK_DIM, 0, IN_W - 8);
  } else {
    if(NV.tab==2&&NV.list.focus>=0&&NV.list.focus<NV.n&&NV.rooms[NV.list.focus].mine){
      if(sk_button(IN_X+4,by,108,TR("Delete room"),!NV.delete_job&&!NV.rename_job&&!NV.lock_job))nav_delete(&NV.rooms[NV.list.focus]);
    }else sk_text(NV.tab == 2 ? TR("Select a room to edit.") : TR("Featured rooms first."), IN_X + 4, by + 3, 9.5f, SK_DIM, 0, 120);
    if (sk_button(IN_X + IN_W - 182, by, 76, NV.job ? TR("Loading…") : TR("Refresh"), !NV.job)) nav_refresh();
    if (sk_button(IN_X + IN_W - 102, by, 100, TR("Create a room"), true) || K.x) { NV.creating = true; NV.new_title[0] = 0; memset(NV.new_password, 0, sizeof NV.new_password); }
  }
}

// ================================================================ catalogue

static struct {
  int page, offer;
  ListState pages, grid;
  bool page_focus;
  u16 bought_def;      /* waiting for this purchase to reach the hand */
  u32 bought_rev;
  int shown[400], shown_n; /* offers on the current page, for the top-screen preview */
} SH = {.page = 0, .offer = -1, .page_focus = true};

static int page_offers(const char *page_id, int *out, int cap) {
  char key[24];
  snprintf(key, sizeof key, ",%s,", page_id);
  int n = 0;
  for (int i = 0; i < cat_offer_count && n < cap; i++)
    if (strstr(cat_offers[i].pages, key)) out[n++] = i;
  return n;
}

static void buy_confirmed(void *ctx) {
  const CatOffer *o = ctx;
  send_buy(o->id);
  SH.bought_def = o->def;
  SH.bought_rev = W.inv_rev;
}

static void place_bought(void *ctx) { (void)ctx; hand_select_def(SH.bought_def); ui_open(P_HAND); SH.bought_def = 0; }

static const char *offer_name(const CatOffer *o) {
  if (o->name[0]) return TR(o->name);
  const FurniDef *d = def_get(o->def);
  return TR(d ? d->name : "Item");
}

static struct { int kind, index; bool trial; char wall[9], floor[9]; u32 revision; } SP = {.kind = 1};

void spaces_reset(bool restore) {
  if (SP.trial && restore && SP.revision == W.finish_rev) {
    snprintf(W.wall, sizeof W.wall, "%s", SP.wall);
    snprintf(W.floor, sizeof W.floor, "%s", SP.floor);
  }
  SP.trial = false;
}

static void finish_buy(void *ctx) {
  const FinishChoice *choice = ctx;
  spaces_reset(true);
  send_decorate(choice->kind, choice->id);
}

static void spaces_draw(float x, float y, float w, u64 now) {
  if (SP.trial && SP.revision != W.finish_rev) spaces_reset(false);
  if (sk_tab(x, y, w / 2, 18, TR("Wallpaper"), SP.kind == 1)) { spaces_reset(true); SP.kind = 1; SP.index = 0; }
  if (sk_tab(x + w / 2, y, w / 2, 18, TR("Floor"), SP.kind == 2)) { spaces_reset(true); SP.kind = 2; SP.index = 0; }
  int choices[256], count = 0;
  for (int i = 0; i < finish_choice_count && count < 256; i++) if (finish_choices[i].kind == SP.kind) choices[count++] = i;
  if (!count) return;
  if (sk_button(x, y + 26, 38, "<", true) || K.left) { spaces_reset(true); SP.index = (SP.index + count - 1) % count; }
  if (sk_button(x + w - 38, y + 26, 38, ">", true) || K.right) { spaces_reset(true); SP.index = (SP.index + 1) % count; }
  const FinishChoice *choice = &finish_choices[choices[SP.index % count]];
  char label[64]; snprintf(label, sizeof label, "%d / %d", SP.index + 1, count);
  sk_text(label, x + w / 2, y + 28, 11, SK_TEXT, 1, w - 80);
  fill_rect(x + 5, y + 51, 26, 26, sk_rgb(SK_INK));
  fill_rect(x + 6, y + 52, 24, 24, sk_rgb(choice->color));
  snprintf(label, sizeof label, "%s #%s", SP.kind == 1 ? "Wallpaper" : "Floor", choice->id);
  sk_text(label, x + 37, y + 57, 10, SK_TEXT, 0, w - 39);
  if (sk_button(x, y + 79, w, SP.trial ? TR("Stop preview") : TR("Preview in room"), true)) {
    if (SP.trial) spaces_reset(true);
    else {
      snprintf(SP.wall, sizeof SP.wall, "%s", W.wall); snprintf(SP.floor, sizeof SP.floor, "%s", W.floor);
      SP.revision = W.finish_rev; SP.trial = true;
      snprintf(SP.kind == 1 ? W.wall : W.floor, 9, "%s", choice->id);
    }
  }
  snprintf(label, sizeof label, TR("Buy - %d credits"), finish_price);
  bool owned = !strcmp(SP.kind == 1 ? (SP.trial ? SP.wall : W.wall) : (SP.trial ? SP.floor : W.floor), choice->id);
  if (sk_button(x, y + 103, w, !world_can_decorate() ? TR("Buy in your own room") : owned ? TR("Already applied") : label, world_can_decorate() && !owned && W.credits >= (u32)finish_price)) {
    snprintf(label, sizeof label, TR("Apply this finish for %d credits?"), finish_price);
    ui_confirm(label, TR("Buy"), finish_buy, (void *)choice);
  }
  (void)now;
}

void shop_draw(u64 now) {
  if (SH.bought_def && W.inv_rev != SH.bought_rev) {
    bool found = false;
    for (int i = 0; i < W.inv_n && !found; i++) if (W.inv[i].def == SH.bought_def) {
      found = true;
      static char q[160];
      const FurniDef *d = def_get(SH.bought_def);
      snprintf(q, sizeof q, TR("%s is in your hand. Place it in your room now?"), d ? d->name : "Your item");
      ui_confirm(q, TR("Place it"), place_bought, NULL);
    }
    SH.bought_rev = W.inv_rev;
    if (!found) SH.bought_def = 0;
  }
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, TR("Catalogue"))) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  // Page column on the right, grouped like the web's catalogue tree: the sections (Furniture
  // lines, Room building, Seasonal…) fold, only the open one lists its pages. L/R jump to the
  // previous or next section; the D-Pad walks the visible rows.
  const float pw = 92, px = IN_X + IN_W - pw - 2, prow = 17;
  fill_rect(px, IN_Y + 2, pw, IN_H - 4, sk_rgb(SK_WHITE));
  static int page_rows[400], groups[32], group_n;
  if (!group_n) {
    for (int i = 0; i < cat_page_count && group_n < (int)COUNT(groups); i++) {
      bool seen = false;
      for (int g = 0; g < group_n; g++) if (!strcmp(cat_pages[groups[g]].group, cat_pages[i].group)) seen = true;
      if (!seen) groups[group_n++] = i;
    }
  }
  const char *open = cat_pages[SH.page].group;
  int open_g = 0;
  for (int g = 0; g < group_n; g++) if (!strcmp(cat_pages[groups[g]].group, open)) open_g = g;
  if (K.down & (KEY_L | KEY_R)) {
    open_g = (open_g + ((K.down & KEY_R) ? 1 : group_n - 1)) % group_n;
    SH.page = groups[open_g]; SH.offer = -1; SH.grid.scroll = 0;
    open = cat_pages[SH.page].group;
  }
  // Visible rows: a section header is -(g + 1), a page is its index.
  int row_n = 0, focus_row = 0;
  for (int g = 0; g < group_n; g++) {
    page_rows[row_n++] = -(g + 1);
    if (g != open_g) continue;
    for (int i = 0; i < cat_page_count && row_n < (int)COUNT(page_rows); i++)
      if (!strcmp(cat_pages[i].group, open)) { if (i == SH.page) focus_row = row_n; page_rows[row_n++] = i; }
  }
  if (SH.page_focus && (K.up || K.dn)) {
    int r = focus_row + (K.dn ? 1 : -1);
    if (r >= 0 && r < row_n) {
      int v = page_rows[r];
      SH.page = v >= 0 ? v : groups[-v - 1];
      SH.offer = -1; SH.grid.scroll = 0;
      if (v < 0) open = cat_pages[SH.page].group;
    }
  }
  if (SH.page_focus && (K.left || K.a)) { SH.page_focus = false; K.a = false; }
  // The D-Pad keeps the focused row in view; a stylus drag clears the focus (sk_list_begin) and
  // the list then stays where it was dragged until the D-Pad is used again.
  if (SH.page_focus && (K.up || K.dn || (K.down & (KEY_L | KEY_R)))) SH.pages.focus = focus_row;
  else if (SH.pages.focus >= 0) SH.pages.focus = focus_row;
  int first = sk_list_begin(&SH.pages, px, IN_Y + 2, pw, IN_H - 4, row_n, prow);
  for (int r = first; r < row_n; r++) {
    float y = sk_list_row_y(r);
    if (y > IN_Y + IN_H) break;
    int v = page_rows[r];
    if (v < 0) {
      const char *name = cat_pages[groups[-v - 1]].group;
      bool here = !strcmp(name, open);
      fill_rect(px, y, pw, prow - 1, sk_rgb(here ? SK_WINDOW : 0xd5dde1));
      text_draw(here ? "-" : "+", px + 5, y + 2, 10, sk_rgb(here ? 0xffffff : SK_TEXT), 0);
      sk_text(TR(name), px + 14, y + 3, 9.5f, here ? 0xffffff : SK_TEXT, 0, pw - 18);
      if (sk_list_row_clicked(r)) { SH.page = groups[-v - 1]; SH.offer = -1; SH.grid.scroll = 0; }
      continue;
    }
    bool on = v == SH.page;
    fill_rect(px, y, pw, prow - 1, sk_rgb(on ? 0xe8eef1 : SK_WHITE));
    if (on) fill_rect(px, y, 3, prow - 1, sk_rgb(SK_WINDOW));
    fill_rect(px + 8, y + prow - 1, pw - 12, 1, sk_rgb(SK_LINE));
    sk_text(TR(cat_pages[v].name), px + 10, y + 3, 9.5f, SK_TEXT, 0, pw - 14);
    if (sk_list_row_clicked(r)) { SH.page = v; SH.offer = -1; SH.grid.scroll = 0; SH.page_focus = false; }
  }
  sk_list_end();

  const CatPage *pg = &cat_pages[SH.page];
  float gx = IN_X + 3, gw = px - gx - 4;
  sk_text(TR(pg->name), gx + 2, IN_Y + 3, 12, SK_TEXT, 0, gw);
  fill_rect(gx, IN_Y + 19, gw, 1, sk_rgb(SK_LINE));

  if (!strcmp(pg->layout, "ctlg_spaces")) { spaces_draw(gx + 2, IN_Y + 25, gw - 4, now); return; }
  spaces_reset(true);
  if(!strcmp(pg->id,"60")){
    sk_paragraph(TR("Donate $5.00 USD or more for Tweebbo Club for life, Club and VIP badges and extra credits. Review the amount before paying."),gx+2,IN_Y+26,gw-4,10,SK_TEXT,7);
    if(sk_button(gx+2,IN_Y+112,gw-4,TR("Get Tweebbo Club"),net_online()))ui_open(P_SUPPORT);
    return;
  }
  if (!strcmp(pg->layout, "ctlg_pets")) {
    sk_paragraph(TR("Dogs, cats and crocodiles with their original coats. Adopt one and bring them to your room."), gx + 2, IN_Y + 26,
                 gw - 4, 10, SK_TEXT, 4);
    if (sk_button(gx + 2, IN_Y + 80, gw - 4, TR("Open Pets"), true)) ui_open(P_PETS);
    return;
  }
  static int offers[400];
  int n = page_offers(pg->id, offers, COUNT(offers));
  memcpy(SH.shown, offers, sizeof(int) * (size_t)n); SH.shown_n = n;
  if (!n) {
    const char *body=!strcmp(pg->id,"1")?TR("Browse the ranges with L/R or the tabs on the right. Seasonal ranges change regularly, so check back for new items."):pg->body[0]?TR(pg->body):TR("Choose a collection on the right to find furniture for your room.");
    sk_paragraph(body, gx + 2, IN_Y + 26,
                 gw - 4, 10, SK_TEXT, 9);
    return;
  }
  // Icon grid (catalog_bg_icon_slot) with details below.
  const float slot = 37, detail_h = 50;
  int cols = (int)(gw / slot);
  int rows = (n + cols - 1) / cols;
  float gy = IN_Y + 22, gh = IN_H - 24 - detail_h;
  bool moved = !SH.page_focus && (K.left || K.right || K.up || K.dn);
  if (!SH.page_focus) {
    if (SH.offer < 0) SH.offer = 0;
    if (K.left) { if (SH.offer % cols == 0) SH.page_focus = true; else SH.offer--; }
    if (K.right && SH.offer + 1 < n && (SH.offer + 1) % cols) SH.offer++;
    if (K.up && SH.offer >= cols) SH.offer -= cols;
    if (K.dn && SH.offer + cols < n) SH.offer += cols;
  }
  if (moved || SH.grid.focus >= 0) SH.grid.focus = SH.offer >= 0 ? SH.offer / cols : -1;
  int frow = sk_list_begin(&SH.grid, gx, gy, gw, gh, rows, slot);
  for (int r = frow; r < rows; r++) {
    float y = sk_list_row_y(r);
    if (y > gy + gh) break;
    for (int c = 0; c < cols; c++) {
      int i = r * cols + c;
      if (i >= n) break;
      const CatOffer *o = &cat_offers[offers[i]];
      float x = gx + c * slot;
      bool sel = i == SH.offer;
      fill_round(x + 1, y + 1, slot - 2, slot - 2, 3, sk_rgb(sel ? SK_SELECT : 0xe4e4e4));
      if (sel) { fill_rect(x + 1, y + 1, slot - 2, 1, sk_rgb(SK_WINDOW)); fill_rect(x + 1, y + slot - 2, slot - 2, 1, sk_rgb(SK_WINDOW)); }
      gfx_furni_icon(o->def, x + slot / 2, y + slot / 2 - 4, slot - 14);
      char tag[12];
      snprintf(tag, sizeof tag, TR("%uc"), (unsigned)o->price);
      sk_text(tag, x + slot / 2, y + slot - 12, 8.5f, o->available && W.credits >= o->price ? SK_DIM : SK_DANGER, 1, slot - 4);
      if (!o->available) fill_round(x + 1, y + 1, slot - 2, slot - 2, 3, sk_rgba(0xffffff, 140));
      if (T.released && T.x0 >= x && T.x0 < x + slot && T.y0 >= y && T.y0 < y + slot && T.y0 >= gy && T.y0 < gy + gh) {
        SH.offer = i;
        SH.page_focus = false;
      }
    }
  }
  sk_list_end();
  float dy = IN_Y + IN_H - detail_h;
  fill_rect(gx, dy - 1, gw, 1, sk_rgb(SK_LINE));
  if (SH.offer >= 0 && SH.offer < n) {
    const CatOffer *o = &cat_offers[offers[SH.offer]];
    sk_text(offer_name(o), gx + 2, dy + 2, 11, SK_TEXT, 0, gw - 4);
    char price[48];
    if (o->amount > 1) snprintf(price, sizeof price, TR("%u credits · %u items"), (unsigned)o->price, o->amount);
    else snprintf(price, sizeof price, TR("%u credits"), (unsigned)o->price);
    sk_text(o->available ? price : TR("Not available yet"), gx + 2, dy + 17, 10, o->available ? SK_DIM : SK_DANGER, 0, gw - 70);
    bool can = o->available && W.credits >= o->price && net_state() != NET_CONNECTING;
    if (sk_button(gx + gw - 66, dy + 14, 64, TR("Buy (A)"), can) || (K.a && can && !SH.page_focus)) {
      static char q[160];
      snprintf(q, sizeof q, TR("Buy %s for %u credits? It will go to your hand."), offer_name(o), (unsigned)o->price);
      ui_confirm(q, TR("Buy"), buy_confirmed, (void *)o);
    }
    if (o->desc[0]) sk_text(TR(o->desc), gx + 2, dy + 33, 9, SK_DIM, 0, gw - 4);
  } else {
    char cr[48];
    snprintf(cr, sizeof cr, TR("%u credits"), (unsigned)W.credits);
    sk_text(TR("Select an item for a closer look."), gx + 2, dy + 4, 10, SK_DIM, 0, gw - 4);
    sk_text(cr, gx + 2, dy + 20, 10, SK_TEXT, 0, gw - 4);
  }
}

// ================================================================ hand (inventory)

typedef struct { u16 def; int count; u32 first; } Stack;

static struct {
  int sel;
  ListState grid;
} HD = {.sel = -1};

static int stacks(Stack *out, int cap) {
  int n = 0;
  for (int i = 0; i < W.inv_n; i++) {
    int k = 0;
    while (k < n && out[k].def != W.inv[i].def) k++;
    if (k == n) { if (n >= cap) continue; out[n++] = (Stack){W.inv[i].def, 0, W.inv[i].id}; }
    out[k].count++;
  }
  return n;
}

void hand_select_def(u16 def) {
  static Stack list[MAX_INV];
  int n = stacks(list, MAX_INV);
  for (int i = 0; i < n; i++) if (list[i].def == def) { HD.sel = i; return; }
}

void hand_draw(u64 now) {
  (void)now;
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, TR("Hand"))) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  static Stack list[MAX_INV];
  int n = stacks(list, MAX_INV);
  if (!n) {
    sk_paragraph(TR("Your hand is empty. Buy furniture in the Catalogue and it will appear here, ready to place in your room."),
                 IN_X + 8, IN_Y + 10, IN_W - 16, 11, SK_TEXT, 5);
    if (sk_button(IN_X + 8, IN_Y + 80, 120, TR("Open Catalogue"), true)) ui_open(P_SHOP);
    return;
  }
  const float slot = 42, detail_h = 44;
  int cols = (int)((IN_W - 4) / slot), rows = (n + cols - 1) / cols;
  float gx = IN_X + 2 + ((IN_W - 4) - cols * slot) / 2, gy = IN_Y + 2, gh = IN_H - 4 - detail_h;
  if (HD.sel >= n) HD.sel = n - 1;
  if (K.left && HD.sel > 0) HD.sel--;
  if (K.right && HD.sel + 1 < n) HD.sel++;
  if (K.up && HD.sel >= cols) HD.sel -= cols;
  if (K.dn && HD.sel + cols < n) HD.sel += cols;
  if ((K.left || K.right || K.up || K.dn) && HD.sel < 0) HD.sel = 0;
  if (K.left || K.right || K.up || K.dn || HD.grid.focus >= 0) HD.grid.focus = HD.sel >= 0 ? HD.sel / cols : -1;
  int frow = sk_list_begin(&HD.grid, IN_X + 2, gy, IN_W - 4, gh, rows, slot);
  for (int r = frow; r < rows; r++) {
    float y = sk_list_row_y(r);
    if (y > gy + gh) break;
    for (int c = 0; c < cols; c++) {
      int i = r * cols + c;
      if (i >= n) break;
      float x = gx + c * slot;
      bool sel = i == HD.sel;
      fill_round(x + 1, y + 1, slot - 2, slot - 2, 3, sk_rgb(sel ? SK_SELECT : SK_WHITE));
      gfx_furni_icon(list[i].def, x + slot / 2, y + slot / 2, slot - 8);
      if (list[i].count > 1) {
        char cnt[8];
        snprintf(cnt, sizeof cnt, "%d", list[i].count);
        fill_round(x + slot - 16, y + slot - 13, 14, 11, 3, sk_rgb(SK_WINDOW));
        text_draw(cnt, x + slot - 9, y + slot - 14, 8.5f, sk_rgb(SK_WHITE), 1);
      }
      if (T.released && T.x0 >= x && T.x0 < x + slot && T.y0 >= y && T.y0 < y + slot && T.y0 >= gy && T.y0 < gy + gh) HD.sel = i;
    }
  }
  sk_list_end();
  float dy = IN_Y + IN_H - detail_h;
  fill_rect(IN_X + 4, dy - 1, IN_W - 8, 1, sk_rgb(SK_LINE));
  if (HD.sel >= 0) {
    const FurniDef *d = def_get(list[HD.sel].def);
    sk_text(TR(d ? d->name : "Item"), IN_X + 6, dy + 3, 11, SK_TEXT, 0, IN_W - 120);
    sk_text(world_can_decorate() ? TR("Place it in your room.") : TR("Furniture can be placed only in rooms you create."), IN_X + 6, dy + 20, 9.5f,
            SK_DIM, 0, IN_W - 120);
    if (sk_button(IN_X + IN_W - 104, dy + 10, 100, TR("Place in room"), world_can_decorate()) || (K.a && world_can_decorate()))
      build_start(list[HD.sel].def, list[HD.sel].first, 0);
  } else sk_text(TR("Select an item."), IN_X + 6, dy + 12, 10.5f, SK_DIM, 0, IN_W - 12);
}

// ================================================================ furniture panel

static u32 item_id(void);
static int drink_sel;

static const char *drink_name(int id) {
  for (int i = 0; i < drink_name_count; i++) if (drink_names[i].id == id) return drink_names[i].name;
  return "Drink";
}

static void redeem_confirmed(void *ctx) { send_redeem((u32)(uintptr_t)ctx); ui_close(); }

void item_draw(u64 now) {
  (void)now;
  Item *it = world_item(item_id());
  if (!it) { ui_close(); return; }
  const FurniDef *d = def_get(it->def);
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, TR(d ? d->name : "Item"))) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  fill_round(IN_X + 4, IN_Y + 4, 60, 60, 4, sk_rgb(SK_WHITE));
  gfx_furni_icon(it->def, IN_X + 34, IN_Y + 34, 54);
  float x = IN_X + 72, w = IN_W - 78;
  sk_text(TR(d ? d->name : "Item"), x, IN_Y + 4, 12, SK_TEXT, 0, w);
  const char *who = it->owner == 0 ? "Fixed room furniture." : it->owner == W.guest_id ? "You own this item." : "Someone else placed this item.";
  sk_text(who, x, IN_Y + 21, 10, SK_DIM, 0, w);
  if (d && d->desc && d->desc[0]) sk_paragraph(TR(d->desc), x, IN_Y + 35, w, 9.5f, SK_DIM, 2);
  float y = IN_Y + 70, bw = (IN_W - 16) / 3;
  int col = 0;
#define NEXT_X (IN_X + 4 + (col % 3) * (bw + 4))
#define NEXT_Y (y + (col / 3) * 22)
  if (d) {
    if (d->drinks && d->drinks[0]) {
      // Dispenser: choose a drink and request it (the avatar walks over by itself).
      int ids[24], n = 0;
      for (const char *p = d->drinks; *p && n < 24;) { ids[n++] = atoi(p); p = strchr(p, ','); if (!p) break; p++; }
      if (drink_sel >= n) drink_sel = 0;
      if (sk_button(IN_X + 4, y, 22, "<", n > 1) || K.left) drink_sel = (drink_sel + n - 1) % n;
      fill_round(IN_X + 30, y, IN_W - 160, 18, 3, sk_rgb(SK_WHITE));
      sk_text(drink_name(ids[drink_sel]), IN_X + 30 + (IN_W - 160) / 2, y + 2, 10.5f, SK_TEXT, 1, IN_W - 168);
      if (sk_button(IN_X + IN_W - 126, y, 22, ">", n > 1) || K.right) drink_sel = (drink_sel + 1) % n;
      if (sk_button(IN_X + IN_W - 100, y, 96, TR("Get a drink"), true) || K.a) { send_drink(it->id, ids[drink_sel]); ui_close(); return; }
      y += 24;
    }
    if (d->kind == KIND_SIT || (d->flags & DEF_LAY)) {
      if (sk_button(NEXT_X, NEXT_Y, bw, (d->flags & DEF_LAY) ? TR("Lie down") : TR("Sit"), true)) { send_walk(it->x, it->y); ui_close(); return; }
      col++;
    }
    if (d->casino || d->kind == KIND_DICE || (d->flags & DEF_TELEPORTER) ||
        (W.room_owner && !d->drink && (d->kind == KIND_TOGGLE || floor_switch(it->def) || (d->kind == KIND_WALL && d->states > 1) || (d->flags & DEF_GATE)))) {
      bool rolling = d->kind == KIND_DICE && it->state == 7;
      const char *label = d->casino ? "Play" : d->kind == KIND_DICE ? (rolling ? "Rolling…" : "Roll") : (d->flags & DEF_TELEPORTER) ? "Join" : "Use";
      if (sk_button(NEXT_X, NEXT_Y, bw, label, !rolling)) send_use(it->id);
      col++;
      // Kepler DICE_OFF: close a die that shows a face (the server checks you stand next to it).
      if (d->kind == KIND_DICE && it->state >= 1 && it->state <= 6) {
        if (sk_button(NEXT_X, NEXT_Y, bw, TR("Close"), true)) send_use_op(it->id, 1);
        col++;
      }
    }
    // Players with rights move and rotate anything here; picking up stays with the item's owner
    // and the room's owner (Kepler's room rights).
    if (world_can_arrange(it) && it->owner) {
      if (sk_button(NEXT_X, NEXT_Y, bw, TR("Move"), world_can_decorate())) { build_start(it->def, 0, it->id); return; }
      col++;
      if (sk_button(NEXT_X, NEXT_Y, bw, TR("Rotate"), true)) send_rotate(it->id);
      col++;
      if (world_can_manage(it)) {
        if (sk_button(NEXT_X, NEXT_Y, bw, TR("Pick up"), true)) { send_pickup(it->id); ui_close(); return; }
        col++;
      }
    }
    if (it->owner == W.guest_id && ((d->flags & DEF_REDEEM) || d->chips)) {
      if (sk_button(NEXT_X, NEXT_Y, bw, TR("Redeem"), true)) {
        // Chip tokens (Tweebbo Exchange) pay chips, the PC Casino's currency; the server keeps a
        // token received in a trade locked for 24 hours and says how long is left.
        static char question[200];
        if (d->chips)
          snprintf(question, sizeof question, "Redeem this token for %lu chips? Chips play in the PC Casino at tweebbo.lol. "
                   "Tokens received in a trade cannot be redeemed for 24 hours (they can still be traded).", (unsigned long)d->chips);
        else snprintf(question, sizeof question,"%s",TR("Redeem this item for credits? It will disappear from the room."));
        ui_confirm(question, TR("Redeem"), redeem_confirmed, (void *)(uintptr_t)it->id);
      }
      col++;
    }
  }
#undef NEXT_X
#undef NEXT_Y
}

// ================================================================ pets

typedef struct { int id, kind, breed, color, hunger, thirst, energy, happiness; u32 owner; char name[24], room[48], action[8]; } PetRow;

static struct {
  int sel; /* selected id; 0 = list */
  bool adopt;
  int kind, breed, color;
  char name[24];
  u32 rev;
  PetRow rows[24];
  int n;
  ListState list;
} PT;

static void pets_parse(void) {
  if (PT.rev == W.pets_rev) return;
  PT.rev = W.pets_rev;
  PT.n = 0;
  json_error_t err;
  json_t *j = W.pet_inv_json ? json_loads(W.pet_inv_json, 0, &err) : NULL;
  size_t i;
  json_t *v;
  json_array_foreach(j, i, v) {
    if (PT.n >= COUNT(PT.rows)) break;
    PetRow *p = &PT.rows[PT.n++];
    memset(p, 0, sizeof *p);
    p->id = (int)json_integer_value(json_object_get(v, "id"));
    p->owner = (u32)json_integer_value(json_object_get(v, "owner"));
    p->kind = (int)json_integer_value(json_object_get(v, "kind"));
    p->breed = (int)json_integer_value(json_object_get(v, "breed"));
    p->color = (int)json_integer_value(json_object_get(v, "color"));
    p->hunger = (int)json_integer_value(json_object_get(v, "hunger"));
    p->thirst = (int)json_integer_value(json_object_get(v, "thirst"));
    p->energy = (int)json_integer_value(json_object_get(v, "energy"));
    p->happiness = (int)json_integer_value(json_object_get(v, "happiness"));
    snprintf(p->name, sizeof p->name, "%s", json_string_value(json_object_get(v, "name")) ?: "Pet");
    snprintf(p->room, sizeof p->room, "%s", json_string_value(json_object_get(v, "room")) ?: "");
    snprintf(p->action, sizeof p->action, "%s", json_string_value(json_object_get(v, "action")) ?: "");
  }
  if (j) json_decref(j);
}

static void pet_send(const char *action, int id) {
  char json[96];
  snprintf(json, sizeof json, "{\"action\":\"%s\",\"id\":%d}", action, id);
  send_pet(json);
}

static void adopt_confirmed(void *ctx) {
  (void)ctx;
  char name[64], json[200];
  json_escape(PT.name, name, sizeof name);
  snprintf(json, sizeof json, "{\"action\":\"adopt\",\"kind\":%d,\"breed\":%d,\"color\":%d,\"name\":\"%s\"}", PT.kind, PT.breed, PT.color, name);
  send_pet(json);
  PT.adopt = false;
}

static void stat_bar(const char *label, int value, float x, float y, float w) {
  sk_text(label, x, y, 9.5f, SK_TEXT, 0, 60);
  fill_round(x + 62, y + 2, w - 98, 9, 3, sk_rgb(0xd0d0d0));
  fill_round(x + 62, y + 2, (w - 98) * (value < 0 ? 0 : value > 100 ? 100 : value) / 100.0f, 9, 3, sk_rgb(SK_GO));
  char t[8];
  snprintf(t, sizeof t, "%d%%", value);
  sk_text(t, x + w - 2, y, 9.5f, SK_DIM, 2, 34);
}

static void pet_name_done(const char *text, void *ctx) { (void)ctx; snprintf(PT.name, sizeof PT.name, "%s", text); }

void pets_draw(u64 now) {
  (void)now;
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, TR("Pets"))) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  if (!online_or_toast()) { ui_close(); return; }
  pets_parse();
  float tw = (IN_W - 8) / 2;
  if (sk_tab(IN_X + 2, IN_Y + 2, tw, 16, TR("My pets"), !PT.adopt)) { PT.adopt = false; PT.sel = 0; send_pet("{\"action\":\"list\"}"); }
  if (sk_tab(IN_X + 4 + tw, IN_Y + 2, tw, 16, TR("Adopt a pet"), PT.adopt)) PT.adopt = true;
  float x = IN_X + 6, y = IN_Y + 24, w = IN_W - 12;
  if (PT.adopt) {
    const PetKind *k = &pet_kinds[PT.kind];
    char line[64];
    sk_text(TR("Species"), x, y + 2, 10, SK_DIM, 0, 60);
    if (sk_button(x + 64, y, 22, "<", true)) { PT.kind = (PT.kind + pet_kind_count - 1) % pet_kind_count; PT.breed = PT.color = 0; }
    sk_text(k->name, x + 64 + (w - 64) / 2, y + 2, 10.5f, SK_TEXT, 1, w - 120);
    if (sk_button(x + w - 22, y, 22, ">", true)) { PT.kind = (PT.kind + 1) % pet_kind_count; PT.breed = PT.color = 0; }
    y += 22;
    sk_text(TR("Coat"), x, y + 2, 10, SK_DIM, 0, 60);
    if (sk_button(x + 64, y, 22, "<", k->breeds > 1)) PT.breed = (PT.breed + k->breeds - 1) % k->breeds;
    snprintf(line, sizeof line, TR("Coat %d"), PT.breed + 1);
    sk_text(line, x + 64 + (w - 64) / 2, y + 2, 10.5f, SK_TEXT, 1, w - 120);
    if (sk_button(x + w - 22, y, 22, ">", k->breeds > 1)) PT.breed = (PT.breed + 1) % k->breeds;
    y += 22;
    sk_text(TR("Color"), x, y + 2, 10, SK_DIM, 0, 60);
    if (sk_button(x + 64, y, 22, "<", true)) PT.color = (PT.color + k->color_count - 1) % k->color_count;
    fill_round(x + 92, y + 2, w - 120, 14, 3, sk_rgb(k->colors[PT.color]));
    if (sk_button(x + w - 22, y, 22, ">", true)) PT.color = (PT.color + 1) % k->color_count;
    y += 22;
    sk_text(TR("Name"), x, y + 2, 10, SK_DIM, 0, 60);
    if (sk_field(x + 64, y, w - 64, PT.name, TR("Your pet's name"))) osk_open(TR("Pet name (2-16 characters)"), PT.name, 16, pet_name_done, NULL);
    y += 26;
    snprintf(line, sizeof line, TR("Adopt · %d credits"), pet_price);
    bool ok = strlen(PT.name) >= 2 && W.credits >= (u32)pet_price;
    if (sk_button_big(x, y, w, line, ok)) {
      static char q[96];
      snprintf(q, sizeof q, TR("Adopting %s costs %d credits."), PT.name, pet_price);
      ui_confirm(q, TR("Confirm adoption"), adopt_confirmed, NULL);
    }
    return;
  }
  PetRow *sel = NULL;
  for (int i = 0; i < PT.n; i++) if (PT.rows[i].id == PT.sel) sel = &PT.rows[i];
  if (!sel) {
    if (!PT.n) { sk_paragraph(TR("Adopt a companion and bring them to your room. All your pets will appear here."), x, y, w, 10.5f, SK_TEXT, 4); return; }
    int first = sk_list_begin(&PT.list, IN_X + 2, y, IN_W - 4, IN_H - 26, PT.n, 22);
    for (int i = first; i < PT.n; i++) {
      float ry = sk_list_row_y(i);
      PetRow *p = &PT.rows[i];
      fill_rect(IN_X + 2, ry, IN_W - 4, 21, sk_rgb(i % 2 ? SK_ROW_ALT : SK_ROW));
      C2D_DrawCircleSolid(IN_X + 14, ry + 10, 0.5f, 6, sk_rgb(pet_kinds[p->kind % pet_kind_count].colors[p->color % 35]));
      sk_text(p->name, IN_X + 26, ry + 4, 10.5f, SK_TEXT, 0, 120);
      sk_text(p->room[0] ? TR("In a room") : TR("With you"), IN_X + 150, ry + 5, 9.5f, SK_DIM, 0, 80);
      if (sk_go(IN_X + IN_W - 52, ry + 2, 46, 17, TR("View"), true) || sk_list_row_clicked(i)) PT.sel = p->id;
    }
    sk_list_end();
    return;
  }
  sk_text(sel->name, x, y, 12, SK_TEXT, 0, w);
  sk_text(pet_kinds[sel->kind % pet_kind_count].name, x, y + 15, 10, SK_DIM, 0, w);
  const char *state = !strcmp(sel->action, "slp") ? "Sleeping" : !strcmp(sel->action, "sit") ? "Sitting" : !strcmp(sel->action, "wlk") ? "Walking" : "Resting";
  sk_text(state, x + w - 4, y, 10, SK_DIM, 2, 100);
  y += 30;
  stat_bar("Fullness", 100 - sel->hunger, x, y, w); y += 13;
  stat_bar("Water", 100 - sel->thirst, x, y, w); y += 13;
  stat_bar("Energy", sel->energy, x, y, w); y += 13;
  stat_bar("Mood", sel->happiness, x, y, w); y += 17;
  if (!sel->room[0]) {
    if (sk_button(x, y, 120, TR("Place in room"), world_can_decorate())) {
      Ent *me = world_ent(W.me);
      if (me) {
        // Next to the player, like dropping it on the web.
        char json[96];
        snprintf(json, sizeof json, "{\"action\":\"place\",\"id\":%d,\"x\":%d,\"y\":%d}", sel->id, me->x + 1, me->y);
        send_pet(json);
      }
    }
    if (!world_can_decorate()) sk_text(TR("Pets can be placed only in rooms you create."), x, y + 22, 9.5f, SK_DIM, 0, w);
  } else if (strcmp(public_slug(sel->room), public_slug(W.slug))) {
    if (sk_button(x, y, 120, TR("Visit room"), true)) { net_goto(public_slug(sel->room), ""); ui_close(); }
  } else {
    static const char *L[][2] = {{"Follow me", "follow"}, {"Stay", "stay"}, {"Sit", "sit"}, {"Sleep", "sleep"},
                                 {"Play", "play"}, {"Feed", "feed"}, {"Give water", "drink"}, {"Pick up pet", "pickup"}};
    float bw = (w - 9) / 4;
    for (int i = 0; i < 8; i++)
      if (sk_button(x + (i % 4) * (bw + 3), y + (i / 4) * 21, bw, L[i][0], true)) pet_send(L[i][1], sel->id);
  }
  if (sk_button(IN_X + IN_W - 60, IN_Y + IN_H - 20, 56, TR("Back"), true)) PT.sel = 0;
}

// The furniture panel keeps its id in ui.c.
extern u32 ui_item_id(void);
static u32 item_id(void) { return ui_item_id(); }

/** Top screen while shopping: a large preview of the selected furniture, its price and how to buy. */
void shop_top(u64 now) {
  (void)now;
  if (ui_panel() != P_SHOP || SH.page_focus || SH.offer < 0 || SH.offer >= SH.shown_n) return;
  const CatOffer *o = &cat_offers[SH.shown[SH.offer]];
  const FurniDef *d = def_get(o->def);
  if (!d || !d->icon_w) return;
  const float w = 150, h = 136, x = 400 - w - 8, y = 8;
  fill_round(x - 1, y - 1, w + 2, h + 2, 6, sk_rgb(0x202020));
  fill_round(x, y, w, h, 5, sk_rgb(0xeeeeee));
  fill_round(x, y, w, 17, 5, sk_rgb(SK_WINDOW));
  sk_text(TR("Catalogue"), x + w / 2, y + 2, 10, SK_WHITE, 1, w - 8);
  // Whole-number zoom keeps the original pixels sharp.
  int box = 72, k = 1;
  while ((k + 1) * d->icon_w <= box && (k + 1) * d->icon_h <= box && k < 4) k++;
  gfx_furni_icon_scaled(o->def, x + w / 2, y + 22 + box / 2, (float)k);
  sk_text(offer_name(o), x + w / 2, y + 98, 10.5f, SK_TEXT, 1, w - 8);
  char line[48];
  snprintf(line, sizeof line, o->amount > 1 ? TR("%u credits · %u items") : TR("%u credits"), (unsigned)o->price, o->amount);
  bool can = o->available && W.credits >= o->price;
  sk_text(o->available ? line : TR("Not available yet"), x + w / 2, y + 111, 9.5f, can ? SK_DIM : SK_DANGER, 1, w - 8);
  sk_text(!o->available ? "" : can ? TR("Press A to buy") : TR("Not enough credits"), x + w / 2, y + 123, 8.5f, SK_DIM, 1, w - 8);
}
