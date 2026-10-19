// Furniture trade with another player in the room (server room/trade.rs). The server owns
// everything: an offer is a list of item ids, nothing moves until both sides confirm, and
// any change clears both confirmations. Credits and chips never travel in a trade.
#include "ui_int.h"
#include "gfx.h"
#include "net.h"
#include "world.h"
#include "catalog.h"
#include <jansson.h>
#include <stdio.h>
#include <string.h>

#define TRADE_MAX 12

typedef struct { u32 id; u16 def; } Offer;

static struct {
  bool active;      /* a trade (invite or open) concerns this player */
  bool open;        /* the invited side accepted: offers can change */
  bool invited_me;  /* the other player invited me and I have not answered */
  bool picking;     /* choosing furniture from the hand */
  bool ok_mine, ok_peer;
  bool confirming;
  u32 revision;
  u32 peer, asked;  /* asked: the player I invited (to tell the two sides apart) */
  char peer_name[NAME_CAP], note[120];
  Offer mine[TRADE_MAX], theirs[TRADE_MAX];
  int mine_n, theirs_n, sel;
  ListState pick;
} T_;

static void act(const char *action, u32 target, u32 id) {
  char json[128];
  snprintf(json, sizeof json, "{\"action\":\"%s\",\"target\":%u,\"id\":%u,\"revision\":%u}", action, (unsigned)target, (unsigned)id,(unsigned)T_.revision);
  send_trade(json);
}

void trade_invite(u32 eid) {
  T_.asked = eid;
  act("invite", eid, 0);
  ui_toast("Trade request sent.", now_ms());
}

static int read_offers(json_t *list, Offer *out) {
  int n = 0; size_t i; json_t *v;
  json_array_foreach(list, i, v) {
    if (n >= TRADE_MAX) break;
    out[n].id = (u32)json_integer_value(json_object_get(v, "id"));
    out[n].def = (u16)json_integer_value(json_object_get(v, "def"));
    n++;
  }
  return n;
}

void trade_receive(const char *text, u64 now) {
  json_t *j = json_loads(text, 0, NULL);
  if (!json_is_object(j)) { json_decref(j); return; }
  const char *stage = json_string_value(json_object_get(j, "stage"));
  const char *note = json_string_value(json_object_get(j, "note"));
  stage = stage ? stage : "";
  if (!strcmp(stage, "done") || !strcmp(stage, "cancelled") || !strcmp(stage, "error")) {
    bool showing = ui_panel() == P_TRADE;
    T_.active = T_.picking = false; T_.asked = 0;
    if (showing) ui_close();
    ui_toast(note && *note ? note : "The trade ended.", now);
    json_decref(j);
    return;
  }
  bool was_active = T_.active;
  T_.active = true;
  T_.open = json_is_true(json_object_get(j, "open"));
  T_.peer = (u32)json_integer_value(json_object_get(j, "peer"));
  snprintf(T_.peer_name, sizeof T_.peer_name, "%s", json_string_value(json_object_get(j, "peerName")) ?: "Player");
  T_.mine_n = read_offers(json_object_get(j, "mine"), T_.mine);
  T_.theirs_n = read_offers(json_object_get(j, "theirs"), T_.theirs);
  T_.ok_mine = json_is_true(json_object_get(j, "okMine"));
  T_.ok_peer = json_is_true(json_object_get(j, "okPeer"));
  T_.confirming=json_is_true(json_object_get(j,"confirming"));
  T_.revision=(u32)json_integer_value(json_object_get(j,"revision"));
  snprintf(T_.note, sizeof T_.note, "%s", note ?: "");
  // The server sends the same "invite" stage to both sides: the one I asked is the inviter.
  if (!was_active) T_.invited_me = !T_.open && T_.peer != T_.asked;
  if (T_.open) T_.invited_me = false;
  if (!was_active || T_.open) ui_open(P_TRADE);
  json_decref(j);
}

static bool offered(u32 id) {
  for (int i = 0; i < T_.mine_n; i++) if (T_.mine[i].id == id) return true;
  return false;
}

static void offer_grid(const Offer *list, int n, float x, float y, float w, bool mine) {
  const float slot = 28;
  int cols = 4;
  fill_round(x, y, w, 3 * slot + 4, 3, sk_rgb(SK_WHITE));
  if (!n) sk_text(mine ? "Add furniture from your hand." : "Nothing offered yet.", x + w / 2, y + slot - 6, 9.5f, SK_DIM, 1, w - 8);
  for (int i = 0; i < n && i < TRADE_MAX; i++) {
    float cx = x + 2 + (i % cols) * slot, cy = y + 2 + (i / cols) * slot;
    gfx_furni_icon(list[i].def, cx + slot / 2, cy + slot / 2, slot - 6);
    if (mine && T_.open && sk_click(cx, cy, slot, slot)) act("remove", 0, list[i].id);
  }
}

static void draw_picker(void) {
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Offer furniture")) { T_.picking = false; return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  u32 ids[MAX_INV]; u16 defs[MAX_INV]; int n = 0;
  for (int i = 0; i < W.inv_n; i++) if (!offered(W.inv[i].id)) { ids[n] = W.inv[i].id; defs[n] = W.inv[i].def; n++; }
  if (!n) {
    sk_paragraph("Your hand has no other furniture to offer. Only furniture in your hand (not placed in a room) can be traded.", IN_X + 8, IN_Y + 10, IN_W - 16, 11, SK_TEXT, 5);
    if (sk_button(IN_X + 8, IN_Y + IN_H - 26, 90, "Back", true) || K.b) T_.picking = false;
    return;
  }
  const float slot = 42;
  int cols = (int)((IN_W - 4) / slot), rows = (n + cols - 1) / cols;
  float gx = IN_X + 2 + ((IN_W - 4) - cols * slot) / 2, gh = IN_H - 30;
  if (T_.sel >= n) T_.sel = n - 1;
  if (T_.sel < 0) T_.sel = 0;
  if (K.left && T_.sel > 0) T_.sel--;
  if (K.right && T_.sel + 1 < n) T_.sel++;
  if (K.up && T_.sel >= cols) T_.sel -= cols;
  if (K.dn && T_.sel + cols < n) T_.sel += cols;
  T_.pick.focus = T_.sel / cols;
  int frow = sk_list_begin(&T_.pick, IN_X + 2, IN_Y + 2, IN_W - 4, gh, rows, slot);
  bool chosen = false;
  for (int r = frow; r < rows; r++) {
    float y = sk_list_row_y(r);
    if (y > IN_Y + 2 + gh) break;
    for (int c = 0; c < cols; c++) {
      int i = r * cols + c;
      if (i >= n) break;
      float x = gx + c * slot;
      fill_round(x + 1, y + 1, slot - 2, slot - 2, 3, sk_rgb(i == T_.sel ? SK_SELECT : SK_WHITE));
      gfx_furni_icon(defs[i], x + slot / 2, y + slot / 2, slot - 8);
      if (sk_click(x, y, slot, slot)) { T_.sel = i; chosen = true; }
    }
  }
  sk_list_end();
  const FurniDef *d = def_get(defs[T_.sel]);
  sk_text(d && d->name ? d->name : "Furniture", IN_X + 6, IN_Y + IN_H - 22, 10, SK_TEXT, 0, IN_W - 110);
  if (sk_button(IN_X + IN_W - 100, IN_Y + IN_H - 24, 96, "Offer (A)", true) || K.a || chosen) {
    if (T_.mine_n >= TRADE_MAX) ui_toast("A trade holds at most 12 items per side.", now_ms());
    else act("add", 0, ids[T_.sel]);
    T_.picking = false;
  }
  if (K.b) T_.picking = false;
}

void trade_draw(u64 now) {
  (void)now;
  if (!T_.active) { ui_close(); return; }
  if (T_.picking) { draw_picker(); return; }
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Trade")) {
    act("cancel", 0, 0); T_.active = false; ui_close(); return;
  }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  char line[120];
  if (!T_.open) {
    if (T_.invited_me) {
      snprintf(line, sizeof line, "%s wants to trade furniture with you.", T_.peer_name);
      sk_paragraph(line, IN_X + 8, IN_Y + 10, IN_W - 16, 12, SK_TEXT, 3);
      sk_paragraph("Nothing moves until both of you confirm the same offer. Credits are never traded.", IN_X + 8, IN_Y + 48, IN_W - 16, 10, SK_DIM, 3);
      if (sk_button_big(IN_X + 8, IN_Y + IN_H - 30, 140, "Accept (A)", true) || K.a) act("accept", 0, 0);
      if (sk_button_big(IN_X + IN_W - 148, IN_Y + IN_H - 30, 140, "Decline (B)", true) || K.b) { act("decline", 0, 0); T_.active = false; ui_close(); }
    } else {
      snprintf(line, sizeof line, "Waiting for %s to accept the trade…", T_.peer_name);
      sk_paragraph(line, IN_X + 8, IN_Y + 20, IN_W - 16, 12, SK_TEXT, 3);
      if (sk_button(IN_X + 8, IN_Y + IN_H - 26, 120, "Cancel (B)", true) || K.b) { act("cancel", 0, 0); T_.active = false; ui_close(); }
    }
    return;
  }
  float half = (IN_W - 12) / 2, y = IN_Y + 2;
  snprintf(line, sizeof line, "%s offers", T_.peer_name);
  sk_text(line, IN_X + 4, y, 10.5f, SK_TEXT, 0, half);
  sk_text("You offer", IN_X + 8 + half, y, 10.5f, SK_TEXT, 0, half);
  offer_grid(T_.theirs, T_.theirs_n, IN_X + 4, y + 16, half, false);
  offer_grid(T_.mine, T_.mine_n, IN_X + 8 + half, y + 16, half, true);
  float sy = y + 106;
  sk_text(T_.ok_peer ? "They confirmed." : "Not confirmed yet.", IN_X + 4, sy, 9.5f, T_.ok_peer ? SK_GO : SK_DIM, 0, half);
  sk_text(T_.ok_mine ? "You confirmed." : "Not confirmed yet.", IN_X + 8 + half, sy, 9.5f, T_.ok_mine ? SK_GO : SK_DIM, 0, half);
  bool tokens = false;
  for (int i = 0; i < T_.mine_n; i++) { const FurniDef *d = def_get(T_.mine[i].def); tokens |= d && d->chips; }
  for (int i = 0; i < T_.theirs_n; i++) { const FurniDef *d = def_get(T_.theirs[i].def); tokens |= d && d->chips; }
  // Same rule as the web (server db.rs CHIP_TRADE_LOCK_SECS): said before anyone confirms.
  if (tokens) sk_text("Received chip tokens: 24 h before redeeming (trading is fine).", IN_X + 4, sy + 13, 9, 0x9c1d23, 0, IN_W - 8);
  else sk_text(T_.confirming?"Review the final offers and confirm again.":T_.mine_n?"Tap your furniture to take it back.":"", IN_X + 4, sy + 13, 9, SK_DIM, 0, IN_W - 8);
  float by = IN_Y + IN_H - 26;
  if (sk_button(IN_X + 4, by, 96, "Add (X)", T_.mine_n < TRADE_MAX) || (K.x && T_.mine_n < TRADE_MAX)) { T_.picking = true; T_.sel = 0; }
  if (sk_button(IN_X + 106, by, 110, T_.ok_mine ? "Accepted" : T_.confirming?"Confirm (A)":"Accept (A)", !T_.ok_mine && (T_.mine_n || T_.theirs_n)) || (K.a && !T_.ok_mine && (T_.mine_n || T_.theirs_n))) act(T_.confirming?"confirm":"ready", 0, 0);
  if (sk_button(IN_X + IN_W - 86, by, 82, "Cancel (B)", true) || K.b) { act("cancel", 0, 0); T_.active = false; ui_close(); }
}
