// Web edition windows ported to the bottom screen: room navigator
// (with creation), catalogue, hand (inventory), furniture panel and pets.
#include <3ds.h>
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
    ui_toast("Offline practice: connect to tweebbo.lol in Help.", now_ms());
    return false;
  }
  return true;
}

// ================================================================ navigator

#define MAX_ROOMS 60
#define MAX_MODELS 16

typedef struct { char slug[48], title[48], owner[24]; int online; bool pub, mine; } RoomRow;

static struct {
  int tab; /* 0 all, 1 public, 2 mine */
  int job, create_job;
  RoomRow rooms[MAX_ROOMS];
  int n;
  struct { char key[24], name[48]; } models[MAX_MODELS];
  int model_n, model;
  bool loaded, creating;
  char error[96], new_title[48];
  u64 refreshed;
  ListState list;
} NV;

static const char *SCOPES[] = {"all", "public", "mine"};

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
        RoomRow *row = &NV.rooms[NV.n++];
        snprintf(row->slug, sizeof row->slug, "%s", json_string_value(json_object_get(v, "slug")) ?: "");
        snprintf(row->title, sizeof row->title, "%s", json_string_value(json_object_get(v, "title")) ?: "");
        snprintf(row->owner, sizeof row->owner, "%s", json_string_value(json_object_get(v, "owner")) ?: "");
        row->online = (int)json_integer_value(json_object_get(v, "online"));
        row->pub = json_is_true(json_object_get(v, "public"));
        row->mine = json_is_true(json_object_get(v, "mine"));
      }
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
  if (NV.create_job && net_http_result(NV.create_job, &r)) {
    NV.create_job = 0;
    json_error_t err;
    json_t *j = r.body ? json_loads(r.body, 0, &err) : NULL;
    const char *slug = j ? json_string_value(json_object_get(j, "slug")) : NULL;
    if (r.status == 200 && slug) {
      ui_toast("Room created. Taking you there…", now_ms());
      NV.creating = false;
      NV.new_title[0] = 0;
      net_goto(slug, "");
      ui_close();
    } else {
      const char *msg = j ? json_string_value(json_object_get(j, "error")) : NULL;
      ui_toast(msg ? msg : r.err[0] ? r.err : "Could not create the room.", now_ms());
    }
    if (j) json_decref(j);
    free(r.body);
  }
}

static void nav_create(void) {
  if (NV.create_job || !NV.model_n) return;
  char title[64];
  int len = (int)strlen(NV.new_title);
  if (len < 3) { ui_toast("Room names need 3 to 40 characters.", now_ms()); return; }
  json_escape(NV.new_title, title, sizeof title);
  char token[200], body[400];
  json_escape(net_token(), token, sizeof token);
  snprintf(body, sizeof body, "{\"title\":\"%s\",\"model\":\"%s\",\"token\":\"%s\"}", title, NV.models[NV.model].key, token);
  NV.create_job = net_http("POST", "/api/rooms", body, false);
}

static void nav_create_view(void) {
  float x = IN_X + 6, y = IN_Y + 4, w = IN_W - 12;
  sk_text("Create a room", x, y, 12, SK_TEXT, 0, w);
  text_draw("Create a room", x + 0.6f, y, 12, sk_rgb(SK_TEXT), 0);
  y += 20;
  sk_text("Room name", x, y, 10, SK_DIM, 0, w);
  if (sk_field(x, y + 13, w, NV.new_title, "Tap to type a name")) {
    char t[64];
    if (ui_keyboard("Room name (3-40 characters)", NV.new_title, t, sizeof t, 40, false)) snprintf(NV.new_title, sizeof NV.new_title, "%s", t);
  }
  y += 40;
  sk_text("Layout", x, y, 10, SK_DIM, 0, w);
  if (NV.model_n) {
    if (sk_button(x, y + 13, 24, "<", true) || K.left) NV.model = (NV.model + NV.model_n - 1) % NV.model_n;
    if (sk_button(x + w - 24, y + 13, 24, ">", true) || K.right) NV.model = (NV.model + 1) % NV.model_n;
    fill_round(x + 28, y + 13, w - 56, 18, 3, sk_rgb(SK_WHITE));
    sk_text(NV.models[NV.model].name, x + w / 2, y + 15, 10.5f, SK_TEXT, 1, w - 64);
  } else sk_text("Loading layouts…", x, y + 15, 10, SK_DIM, 0, w);
  y += 44;
  sk_paragraph("You can create up to five rooms. Only you can decorate them; everyone can visit.", x, y, w, 10, SK_DIM, 2);
  float by = IN_Y + IN_H - 26, bw = (w - 8) / 2;
  if (sk_button(x, by, bw, "Back", true)) NV.creating = false;
  if (sk_button_big(x + bw + 8, by - 3, bw, NV.create_job ? "Creating…" : "Create room",
                    !NV.create_job && strlen(NV.new_title) >= 3 && NV.model_n) || (K.a && strlen(NV.new_title) >= 3))
    nav_create();
}

void nav_draw(u64 now) {
  nav_poll();
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Navigator")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  if (net_state() == NET_OFFLINE) {
    sk_paragraph("Offline practice: only the lobby is available. Open Help to connect to tweebbo.lol or a local server.",
                 IN_X + 8, IN_Y + 10, IN_W - 16, 11, SK_TEXT, 5);
    return;
  }
  if (NV.creating) { nav_create_view(); return; }
  static const char *TABS[] = {"All Rooms", "Public Rooms", "My Rooms"};
  float tw = (IN_W - 8) / 3;
  for (int i = 0; i < 3; i++)
    if (sk_tab(IN_X + 2 + i * (tw + 2), IN_Y + 2, tw, 16, TABS[i], NV.tab == i) && NV.tab != i) {
      NV.tab = i; NV.n = 0; NV.loaded = false; NV.list.scroll = 0; nav_refresh();
    }
  if (K.left || K.right) { NV.tab = (NV.tab + (K.right ? 1 : 2)) % 3; NV.n = 0; NV.loaded = false; nav_refresh(); }
  // An open navigator refreshes every 15 s, like the web.
  if (now - NV.refreshed > 15000) nav_refresh();
  float ly = IN_Y + 22, lh = IN_H - 48;
  fill_rect(IN_X + 2, ly, IN_W - 4, lh, sk_rgb(SK_WHITE));
  const float row = 26;
  if (K.up) NV.list.focus = NV.list.focus <= 0 ? 0 : NV.list.focus - 1;
  if (K.dn) NV.list.focus = NV.list.focus + 1 >= NV.n ? NV.n - 1 : NV.list.focus + 1;
  int first = sk_list_begin(&NV.list, IN_X + 2, ly, IN_W - 4, lh, NV.n, row);
  for (int i = first; i < NV.n; i++) {
    float y = sk_list_row_y(i);
    if (y > ly + lh) break;
    RoomRow *r = &NV.rooms[i];
    bool focus = NV.list.focus == i;
    fill_rect(IN_X + 2, y, IN_W - 4, row - 1, sk_rgb(focus ? SK_SELECT : i % 2 ? SK_ROW_ALT : SK_ROW));
    fill_rect(IN_X + 2, y + row - 1, IN_W - 4, 1, sk_rgb(SK_LINE));
    sk_sprite("building_icon@18", IN_X + 5, y + 4, 1);
    sk_text(r->title, IN_X + 28, y + 2, 10.5f, SK_TEXT, 0, IN_W - 120);
    char sub[48];
    if (r->pub) snprintf(sub, sizeof sub, "Public room");
    else snprintf(sub, sizeof sub, "by %s", r->owner);
    sk_text(sub, IN_X + 28, y + 13, 9, SK_DIM, 0, IN_W - 120);
    char inside[16];
    snprintf(inside, sizeof inside, "%d inside", r->online);
    sk_text(inside, IN_X + IN_W - 50, y + 7, 9, SK_DIM, 2, 60);
    bool here = !strcmp(public_slug(W.slug), r->slug);
    if (sk_go(IN_X + IN_W - 44, y + 4, 34, 18, here ? "Here" : "Go", r->online > 0) || (focus && K.a)) {
      if (!here) { net_goto(r->slug, ""); ui_close(); }
    }
  }
  sk_list_end();
  if (!NV.n) sk_text(NV.error[0] ? NV.error : NV.job ? "Loading rooms…" : NV.tab == 2 ? "You have not created any rooms yet." : "No rooms.",
                     BOT_W / 2, ly + 30, 10.5f, NV.error[0] ? SK_DANGER : SK_DIM, 1, IN_W - 20);
  float by = IN_Y + IN_H - 22;
  sk_text("Most populated rooms first.", IN_X + 4, by + 3, 9.5f, SK_DIM, 0, 120);
  if (sk_button(IN_X + IN_W - 182, by, 76, NV.job ? "Loading…" : "Refresh", !NV.job)) nav_refresh();
  if (sk_button(IN_X + IN_W - 102, by, 100, "Create a room", true) || K.x) { NV.creating = true; NV.new_title[0] = 0; }
}

// ================================================================ catalogue

static struct {
  int page, offer;
  ListState pages, grid;
  bool page_focus;
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
}

static const char *offer_name(const CatOffer *o) {
  if (o->name[0]) return o->name;
  const FurniDef *d = def_get(o->def);
  return d ? d->name : "Item";
}

void shop_draw(u64 now) {
  (void)now;
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Catalogue")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  // Page column on the right, like the classic catalogue.
  const float pw = 92, px = IN_X + IN_W - pw - 2, prow = 17;
  fill_rect(px, IN_Y + 2, pw, IN_H - 4, sk_rgb(SK_WHITE));
  if (SH.page_focus) {
    if (K.up) SH.page = SH.page > 0 ? SH.page - 1 : 0;
    if (K.dn) SH.page = SH.page + 1 < cat_page_count ? SH.page + 1 : SH.page;
    if (K.up || K.dn) { SH.offer = -1; SH.grid.scroll = 0; }
    if (K.left || K.a) { SH.page_focus = false; K.a = false; }
  }
  SH.pages.focus = SH.page;
  int first = sk_list_begin(&SH.pages, px, IN_Y + 2, pw, IN_H - 4, cat_page_count, prow);
  for (int i = first; i < cat_page_count; i++) {
    float y = sk_list_row_y(i);
    if (y > IN_Y + IN_H) break;
    bool on = i == SH.page;
    fill_rect(px, y, pw, prow - 1, sk_rgb(on ? 0xe8eef1 : SK_WHITE));
    if (on) fill_rect(px, y, 3, prow - 1, sk_rgb(SK_WINDOW));
    fill_rect(px + 4, y + prow - 1, pw - 8, 1, sk_rgb(SK_LINE));
    sk_text(cat_pages[i].name, px + 7, y + 3, 9.5f, SK_TEXT, 0, pw - 12);
    if (sk_list_row_clicked(i)) { SH.page = i; SH.offer = -1; SH.grid.scroll = 0; SH.page_focus = false; }
  }
  sk_list_end();

  const CatPage *pg = &cat_pages[SH.page];
  float gx = IN_X + 3, gw = px - gx - 4;
  sk_text(pg->name, gx + 2, IN_Y + 3, 12, SK_TEXT, 0, gw);
  text_draw(pg->name, gx + 2.6f, IN_Y + 3, 12, sk_rgb(SK_TEXT), 0);
  fill_rect(gx, IN_Y + 19, gw, 1, sk_rgb(SK_LINE));

  if (!strcmp(pg->layout, "ctlg_pets")) {
    sk_paragraph("Dogs, cats and crocodiles with their original coats. Adopt one and bring them to your room.", gx + 2, IN_Y + 26,
                 gw - 4, 10, SK_TEXT, 4);
    if (sk_button(gx + 2, IN_Y + 80, gw - 4, "Open Pets", true)) ui_open(P_PETS);
    return;
  }
  static int offers[400];
  int n = page_offers(pg->id, offers, COUNT(offers));
  if (!n) {
    sk_paragraph(pg->body[0] ? pg->body : "Choose a collection on the right to find furniture for your room.", gx + 2, IN_Y + 26,
                 gw - 4, 10, SK_TEXT, 9);
    return;
  }
  // Icon grid (catalog_bg_icon_slot) with details below.
  const float slot = 37, detail_h = 50;
  int cols = (int)(gw / slot);
  int rows = (n + cols - 1) / cols;
  float gy = IN_Y + 22, gh = IN_H - 24 - detail_h;
  if (!SH.page_focus) {
    if (SH.offer < 0) SH.offer = 0;
    if (K.left) { if (SH.offer % cols == 0) SH.page_focus = true; else SH.offer--; }
    if (K.right && SH.offer + 1 < n && (SH.offer + 1) % cols) SH.offer++;
    if (K.up && SH.offer >= cols) SH.offer -= cols;
    if (K.dn && SH.offer + cols < n) SH.offer += cols;
  }
  SH.grid.focus = SH.offer >= 0 ? SH.offer / cols : -1;
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
      gfx_furni_icon(o->def, x + slot / 2, y + slot / 2, slot - 6);
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
    if (o->amount > 1) snprintf(price, sizeof price, "%u credits · %u items", (unsigned)o->price, o->amount);
    else snprintf(price, sizeof price, "%u credits", (unsigned)o->price);
    sk_text(o->available ? price : "Not available yet", gx + 2, dy + 17, 10, o->available ? SK_DIM : SK_DANGER, 0, gw - 70);
    bool can = o->available && W.credits >= o->price && net_state() != NET_CONNECTING;
    if (sk_button(gx + gw - 62, dy + 14, 60, "Buy", can) || (K.a && can && !SH.page_focus)) {
      static char q[160];
      snprintf(q, sizeof q, "Buy %s for %u credits? It will go to your hand.", offer_name(o), (unsigned)o->price);
      ui_confirm(q, "Buy", buy_confirmed, (void *)o);
    }
    if (o->desc[0]) sk_text(o->desc, gx + 2, dy + 33, 9, SK_DIM, 0, gw - 4);
  } else {
    char cr[48];
    snprintf(cr, sizeof cr, "%u credits", (unsigned)W.credits);
    sk_text("Select an item for a closer look.", gx + 2, dy + 4, 10, SK_DIM, 0, gw - 4);
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

void hand_draw(u64 now) {
  (void)now;
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Hand")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  static Stack list[MAX_INV];
  int n = stacks(list, MAX_INV);
  if (!n) {
    sk_paragraph("Your hand is empty. Buy furniture in the Catalogue and it will appear here, ready to place in your room.",
                 IN_X + 8, IN_Y + 10, IN_W - 16, 11, SK_TEXT, 5);
    if (sk_button(IN_X + 8, IN_Y + 80, 120, "Open Catalogue", true)) ui_open(P_SHOP);
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
  HD.grid.focus = HD.sel >= 0 ? HD.sel / cols : -1;
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
    sk_text(d ? d->name : "Item", IN_X + 6, dy + 3, 11, SK_TEXT, 0, IN_W - 120);
    sk_text(world_can_decorate() ? "Place it in your room." : "Furniture can be placed only in rooms you create.", IN_X + 6, dy + 20, 9.5f,
            SK_DIM, 0, IN_W - 120);
    if (sk_button(IN_X + IN_W - 104, dy + 10, 100, "Place in room", world_can_decorate()) || (K.a && world_can_decorate()))
      build_start(list[HD.sel].def, list[HD.sel].first, 0);
  } else sk_text("Select an item.", IN_X + 6, dy + 12, 10.5f, SK_DIM, 0, IN_W - 12);
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
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, d ? d->name : "Item")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  fill_round(IN_X + 4, IN_Y + 4, 60, 60, 4, sk_rgb(SK_WHITE));
  gfx_furni_icon(it->def, IN_X + 34, IN_Y + 34, 54);
  float x = IN_X + 72, w = IN_W - 78;
  sk_text(d ? d->name : "Item", x, IN_Y + 4, 12, SK_TEXT, 0, w);
  const char *who = it->owner == 0 ? "Fixed room furniture." : it->owner == W.guest_id ? "You own this item." : "Someone else placed this item.";
  sk_text(who, x, IN_Y + 21, 10, SK_DIM, 0, w);
  if (d && d->desc && d->desc[0]) sk_paragraph(d->desc, x, IN_Y + 35, w, 9.5f, SK_DIM, 2);
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
      if (sk_button(IN_X + IN_W - 100, y, 96, "Get a drink", true) || K.a) { send_drink(it->id, ids[drink_sel]); ui_close(); return; }
      y += 24;
    }
    if (d->kind == KIND_SIT || (d->flags & DEF_LAY)) {
      if (sk_button(NEXT_X, NEXT_Y, bw, (d->flags & DEF_LAY) ? "Lie down" : "Sit", true)) { send_walk(it->x, it->y); ui_close(); return; }
      col++;
    }
    if (d->kind == KIND_TOGGLE || d->kind == KIND_DICE || (d->flags & (DEF_TELEPORTER | DEF_GATE))) {
      const char *label = d->kind == KIND_DICE ? "Roll" : (d->flags & DEF_TELEPORTER) ? "Join" : "Use";
      if (sk_button(NEXT_X, NEXT_Y, bw, label, true)) send_use(it->id);
      col++;
    }
    if (world_can_manage(it) && it->owner) {
      if (sk_button(NEXT_X, NEXT_Y, bw, "Move", world_can_decorate())) { build_start(it->def, 0, it->id); return; }
      col++;
      if (sk_button(NEXT_X, NEXT_Y, bw, "Rotate", true)) send_rotate(it->id);
      col++;
      if (sk_button(NEXT_X, NEXT_Y, bw, "Pick up", true)) { send_pickup(it->id); ui_close(); return; }
      col++;
    }
    if (it->owner == W.guest_id && (d->flags & DEF_REDEEM)) {
      if (sk_button(NEXT_X, NEXT_Y, bw, "Redeem", true))
        ui_confirm("Redeem this item for credits? It will disappear from the room.", "Redeem", redeem_confirmed, (void *)(uintptr_t)it->id);
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

void pets_draw(u64 now) {
  (void)now;
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Pets")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  if (!online_or_toast()) { ui_close(); return; }
  pets_parse();
  float tw = (IN_W - 8) / 2;
  if (sk_tab(IN_X + 2, IN_Y + 2, tw, 16, "My pets", !PT.adopt)) { PT.adopt = false; PT.sel = 0; send_pet("{\"action\":\"list\"}"); }
  if (sk_tab(IN_X + 4 + tw, IN_Y + 2, tw, 16, "Adopt a pet", PT.adopt)) PT.adopt = true;
  float x = IN_X + 6, y = IN_Y + 24, w = IN_W - 12;
  if (PT.adopt) {
    const PetKind *k = &pet_kinds[PT.kind];
    char line[64];
    sk_text("Species", x, y + 2, 10, SK_DIM, 0, 60);
    if (sk_button(x + 64, y, 22, "<", true)) { PT.kind = (PT.kind + pet_kind_count - 1) % pet_kind_count; PT.breed = PT.color = 0; }
    sk_text(k->name, x + 64 + (w - 64) / 2, y + 2, 10.5f, SK_TEXT, 1, w - 120);
    if (sk_button(x + w - 22, y, 22, ">", true)) { PT.kind = (PT.kind + 1) % pet_kind_count; PT.breed = PT.color = 0; }
    y += 22;
    sk_text("Coat", x, y + 2, 10, SK_DIM, 0, 60);
    if (sk_button(x + 64, y, 22, "<", k->breeds > 1)) PT.breed = (PT.breed + k->breeds - 1) % k->breeds;
    snprintf(line, sizeof line, "Coat %d", PT.breed + 1);
    sk_text(line, x + 64 + (w - 64) / 2, y + 2, 10.5f, SK_TEXT, 1, w - 120);
    if (sk_button(x + w - 22, y, 22, ">", k->breeds > 1)) PT.breed = (PT.breed + 1) % k->breeds;
    y += 22;
    sk_text("Color", x, y + 2, 10, SK_DIM, 0, 60);
    if (sk_button(x + 64, y, 22, "<", true)) PT.color = (PT.color + k->color_count - 1) % k->color_count;
    fill_round(x + 92, y + 2, w - 120, 14, 3, sk_rgb(k->colors[PT.color]));
    if (sk_button(x + w - 22, y, 22, ">", true)) PT.color = (PT.color + 1) % k->color_count;
    y += 22;
    sk_text("Name", x, y + 2, 10, SK_DIM, 0, 60);
    if (sk_field(x + 64, y, w - 64, PT.name, "Your pet's name")) {
      char t[32];
      if (ui_keyboard("Pet name (2-16 characters)", PT.name, t, sizeof t, 16, false)) snprintf(PT.name, sizeof PT.name, "%s", t);
    }
    y += 26;
    snprintf(line, sizeof line, "Adopt · %d credits", pet_price);
    bool ok = strlen(PT.name) >= 2 && W.credits >= (u32)pet_price;
    if (sk_button_big(x, y, w, line, ok)) {
      static char q[96];
      snprintf(q, sizeof q, "Adopting %s costs %d credits.", PT.name, pet_price);
      ui_confirm(q, "Confirm adoption", adopt_confirmed, NULL);
    }
    return;
  }
  PetRow *sel = NULL;
  for (int i = 0; i < PT.n; i++) if (PT.rows[i].id == PT.sel) sel = &PT.rows[i];
  if (!sel) {
    if (!PT.n) { sk_paragraph("Adopt a companion and bring them to your room. All your pets will appear here.", x, y, w, 10.5f, SK_TEXT, 4); return; }
    int first = sk_list_begin(&PT.list, IN_X + 2, y, IN_W - 4, IN_H - 26, PT.n, 22);
    for (int i = first; i < PT.n; i++) {
      float ry = sk_list_row_y(i);
      PetRow *p = &PT.rows[i];
      fill_rect(IN_X + 2, ry, IN_W - 4, 21, sk_rgb(i % 2 ? SK_ROW_ALT : SK_ROW));
      C2D_DrawCircleSolid(IN_X + 14, ry + 10, 0.5f, 6, sk_rgb(pet_kinds[p->kind % pet_kind_count].colors[p->color % 35]));
      sk_text(p->name, IN_X + 26, ry + 4, 10.5f, SK_TEXT, 0, 120);
      sk_text(p->room[0] ? "In a room" : "With you", IN_X + 150, ry + 5, 9.5f, SK_DIM, 0, 80);
      if (sk_go(IN_X + IN_W - 52, ry + 2, 46, 17, "View", true) || sk_list_row_clicked(i)) PT.sel = p->id;
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
    if (sk_button(x, y, 120, "Place in room", world_can_decorate())) {
      Ent *me = world_ent(W.me);
      if (me) {
        // Next to the player, like dropping it on the web.
        char json[96];
        snprintf(json, sizeof json, "{\"action\":\"place\",\"id\":%d,\"x\":%d,\"y\":%d}", sel->id, me->x + 1, me->y);
        send_pet(json);
      }
    }
    if (!world_can_decorate()) sk_text("Pets can be placed only in rooms you create.", x, y + 22, 9.5f, SK_DIM, 0, w);
  } else if (strcmp(public_slug(sel->room), public_slug(W.slug))) {
    if (sk_button(x, y, 120, "Visit room", true)) { net_goto(public_slug(sel->room), ""); ui_close(); }
  } else {
    static const char *L[][2] = {{"Follow me", "follow"}, {"Stay", "stay"}, {"Sit", "sit"}, {"Sleep", "sleep"},
                                 {"Play", "play"}, {"Feed", "feed"}, {"Give water", "drink"}, {"Pick up pet", "pickup"}};
    float bw = (w - 9) / 4;
    for (int i = 0; i < 8; i++)
      if (sk_button(x + (i % 4) * (bw + 3), y + (i / 4) * 21, bw, L[i][0], true)) pet_send(L[i][1], sel->id);
  }
  if (sk_button(IN_X + IN_W - 60, IN_Y + IN_H - 20, 56, "Back", true)) PT.sel = 0;
}

// The furniture panel keeps its id in ui.c.
extern u32 ui_item_id(void);
static u32 item_id(void) { return ui_item_id(); }
