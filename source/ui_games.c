#include "ui_int.h"
#include "net.h"
#include "world.h"
#include "gfx.h"
#include "snow.h"
#include "scene.h"
#include "proto.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static u32 challenge_target, seen_revision;
static int picked = -1, promotion = 5;
static const char *game_name(int kind) { return kind == 1 ? "Tic-tac-toe" : kind == 2 ? "Connect Four" : "Chess"; }
static const char *player_name(u32 id) { Ent *e = world_ent(id); return e ? e->name : "Player"; }
void games_challenge(u32 target) { challenge_target = target; ui_open(P_GAME); }

void games_poll(void) {
  if (seen_revision == W.game_rev) return;
  seen_revision = W.game_rev; picked = -1;
  for (int i = 0; i < 8; i++) if (W.invites[i].from) {
    if (ui_chat_blocked(W.invites[i].from)) { send_game(2, W.invites[i].from, 0, 0, 0); W.invites[i].from = 0; }
    else { challenge_target = 0; ui_open(P_GAME); }
  }
  if (W.duel.id) { challenge_target = 0; ui_open(P_GAME); }
}

static void resign(void *ctx) { (void)ctx; send_game(4, W.duel.id, 0, 0, 0); ui_close(); }
static bool legal(int from, int to) {
  for (int i = 0; i < W.duel.move_count; i++) if (W.duel.moves[i][0] == from && W.duel.moves[i][1] == to) return true;
  return false;
}
static void move(int from, int to) { send_game(3, W.duel.id, from, to, promotion); W.duel.move_count = 0; picked = -1; }

void games_draw(u64 now) {
  Duel *g = &W.duel;
  bool close = sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, challenge_target || !g->id ? "Challenge" : game_name(g->kind));
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  if (close || K.b) {
    if (g->id && !g->result && !challenge_target) ui_confirm("Resign from this game?", "Resign", resign, NULL);
    else { challenge_target = 0; ui_close(); }
    return;
  }
  for (int i = 0; i < 8; i++) if (W.invites[i].from) {
    GameInvite *inv = &W.invites[i];
    char line[120]; snprintf(line, sizeof line, "%s challenges you to %s.", player_name(inv->from), game_name(inv->kind));
    sk_paragraph(line, IN_X + 12, IN_Y + 18, IN_W - 24, 12, SK_TEXT, 4);
    if (sk_button(IN_X + 12, IN_Y + 100, 130, "Decline", true)) { send_game(2, inv->from, 0, 0, 0); inv->from = 0; }
    if (sk_button(IN_X + 154, IN_Y + 100, 130, "Accept", true) || K.a) { send_game(2, inv->from, 1, 0, 0); inv->from = 0; }
    return;
  }
  if (challenge_target) {
    sk_text(player_name(challenge_target), IN_X + 12, IN_Y + 8, 12, SK_TEXT, 0, IN_W - 24);
    for (int kind = 1; kind <= 3; kind++) if (sk_button(IN_X + 30, IN_Y + 14 + kind * 32, IN_W - 60, game_name(kind), true)) {
      send_game(1, challenge_target, kind, 0, 0); challenge_target = 0;
      ui_toast("Challenge sent.", now); ui_close();
    }
    return;
  }
  if (!g->id) { sk_text("Waiting for a game…", 160, IN_Y + 30, 12, SK_TEXT, 1, 280); return; }
  bool mine = !g->result && g->players[g->turn] == W.me;
  const char *status = g->result == 3 ? "Draw" : g->result ? (g->players[g->result - 1] == W.me ? "You win!" : "You lost") : mine ? (g->check ? "Check! Your move" : "Your move") : "Waiting for opponent";
  sk_text(status, 207, IN_Y + 8, 11, SK_TEXT, 0, 99);
  sk_text(player_name(g->players[0]), 207, IN_Y + 30, 10, SK_TEXT, 0, 99);
  sk_text(player_name(g->players[1]), 207, IN_Y + 45, 10, SK_TEXT, 0, 99);
  int cols = g->kind == 1 ? 3 : g->kind == 2 ? 7 : 8;
  int rows = g->kind == 1 ? 3 : g->kind == 2 ? 6 : 8;
  int size = g->kind == 1 ? 45 : g->kind == 2 ? 24 : 19;
  int ox = IN_X + 8, oy = IN_Y + 3;
  for (int y = 0; y < rows; y++) for (int x = 0; x < cols; x++) {
    bool flip = g->kind == 3 && g->players[1] == W.me;
    int cell = g->kind == 1 ? y * 3 + x : g->kind == 2 ? (5 - y) * 7 + x : flip ? y * 8 + 7 - x : (7 - y) * 8 + x;
    int px = ox + x * size, py = oy + y * size;
    u8 piece = g->board[cell];
    bool option = mine && (g->kind == 3 ? picked >= 0 && legal(picked, cell) : legal(g->kind == 2 ? x : cell, 0));
    u32 color = picked == cell && g->kind == 3 ? 0xffd34d : option ? 0xcfe3ec : (x + y) % 2 ? 0xb2aa94 : 0xf5efdc;
    fill_rect(px, py, size - 1, size - 1, sk_rgb(color));
    if (piece) {
      if (g->kind == 3) {
        static const char *pieces[] = {"", "P", "N", "B", "R", "Q", "K"};
        unsigned type = piece & 7;
        if (type <= 6) {
          fill_round(px + 2, py + 2, size - 5, size - 5, 2, sk_rgb(piece & 8 ? 0x222222 : 0xffffff));
          text_draw(pieces[type], px + (size - 1) / 2.f, py + 2, 11, sk_rgb(piece & 8 ? 0xffffff : 0x222222), 1);
        }
      } else if (g->kind == 2) fill_round(px + 3, py + 3, size - 7, size - 7, (size - 7) / 2.f, sk_rgb(piece == 1 ? 0xb33a2c : 0xffd34d));
      else text_draw(piece == 1 ? "X" : "O", px + size / 2.f, py + (size - 17) / 2.f, 15, sk_rgb(piece == 1 ? 0xb33a2c : 0x14434c), 1);
    }
    if (mine && sk_hit(px, py, size - 1, size - 1)) {
      if (g->kind != 3) { int from = g->kind == 2 ? x : cell; if (legal(from, 0)) move(from, 0); }
      else if (picked >= 0 && legal(picked, cell)) move(picked, cell);
      else { picked = -1; for (int i = 0; i < g->move_count; i++) if (g->moves[i][0] == cell) picked = cell; }
    }
  }
  if (g->kind == 3) {
    static const int promos[] = {5, 4, 3, 2};
    static const char *labels[] = {"Queen", "Rook", "Bishop", "Knight"};
    int at = 0; while (at < 3 && promos[at] != promotion) at++;
    sk_text("Promotion:", 207, IN_Y + 68, 10, SK_DIM, 0, 95);
    if (sk_button(207, IN_Y + 83, 95, labels[at], true)) promotion = promos[(at + 1) % 4];
  }
  if (sk_button(207, IN_Y + 126, 95, g->result ? "Close" : "Resign", true)) {
    if (g->result) { g->id = 0; ui_close(); }
    else ui_confirm("Resign from this game?", "Resign", resign, NULL);
  }
}

// ---------------------------------------------------------------- games hub

#define STAFF_GUESTS 4001000000u  /* public-room staff (server population.rs STAFF_BASE) */
static int hub_kind = 1;
static ListState hub_list;

void games_hub_draw(u64 now) {
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Games")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  float x = IN_X + 4, w = IN_W - 8, y = IN_Y + 4;
  // Snow Storm, like the web's Games sheet.
  fill_round(x, y, w, 36, 4, sk_rgb(0xe4ecef));
  sk_sprite("games-icon", x + 4, y + 3, 1);
  sk_text("Snow Storm", x + 40, y + 3, 11, SK_TEXT, 0, w - 120);
  sk_text("Team snowball fight, or practise with bots", x + 40, y + 18, 9, SK_DIM, 0, w - 120);
  if (sk_button(x + w - 72, y + 8, 68, "Play (Y)", net_online()) || (K.y && net_online())) { snow_open(); return; }
  y += 42;
  sk_text("Challenge a player", x, y, 10.5f, SK_TEXT, 0, w);
  y += 15;
  float tw = (w - 4) / 3;
  for (int k = 1; k <= 3; k++)
    if (sk_tab(x + (k - 1) * (tw + 2), y, tw, 16, game_name(k), hub_kind == k)) hub_kind = k;
  if (K.left) hub_kind = hub_kind == 1 ? 3 : hub_kind - 1;
  if (K.right) hub_kind = hub_kind == 3 ? 1 : hub_kind + 1;
  y += 20;
  // People in this room (not you, not staff), alphabetically like the web.
  Ent *people[MAX_ENTS];
  int n = 0;
  for (int i = 0; i < MAX_ENTS; i++) {
    Ent *e = &W.ents[i];
    if (!e->used || e->id == W.me || !e->guest || e->guest >= STAFF_GUESTS) continue;
    int at = n++;
    while (at > 0 && strcasecmp(people[at - 1]->name, e->name) > 0) { people[at] = people[at - 1]; at--; }
    people[at] = e;
  }
  float lh = IN_Y + IN_H - y - 2;
  fill_rect(x, y, w, lh, sk_rgb(SK_WHITE));
  if (!n) { sk_paragraph("Nobody else is in this room right now. Invite a friend and challenge them here.", x + 6, y + 8, w - 12, 10, SK_DIM, 3); return; }
  const float row = 22;
  if (K.up) hub_list.focus = hub_list.focus <= 0 ? 0 : hub_list.focus - 1;
  if (K.dn) hub_list.focus = hub_list.focus + 1 >= n ? n - 1 : hub_list.focus + 1;
  int first = sk_list_begin(&hub_list, x, y, w, lh, n, row);
  for (int i = first; i < n; i++) {
    float ry = sk_list_row_y(i);
    if (ry > y + lh) break;
    bool focus = hub_list.focus == i;
    fill_rect(x, ry, w, row - 1, sk_rgb(focus ? SK_SELECT : i % 2 ? SK_ROW_ALT : SK_ROW));
    sk_text(people[i]->name, x + 6, ry + 5, 10.5f, SK_TEXT, 0, w - 96);
    int dev = people[i]->device ? people[i]->device : (people[i]->flags & FLAG_3DS ? DEVICE_3DS : 0);
    if (dev) device_badge(dev, x + w - 96, ry + 4, 0.6f);
    if (sk_go(x + w - 78, ry + 2, 74, 18, "Challenge", true) || (focus && K.a)) {
      send_game(1, people[i]->id, hub_kind, 0, 0);
      ui_toast("Challenge sent. They have 30 seconds to accept.", now);
      ui_close();
      return;
    }
  }
  sk_list_end();
}
