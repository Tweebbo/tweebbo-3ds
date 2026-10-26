#include "language.h"
// After Hours Bar on the console: the menu comes from the server (prices and contents are
// decided there, server room/bar.rs); orders walk the guest to a free stool and Vic serves.
#include "bar.h"
#include "ui_int.h"
#include "avatar.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "scene.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MENU_MAX 24
#define STAFF_BASE 4001000000u
typedef struct { int id, price; bool smoke; char name[40], about[64]; } MenuItem;
typedef struct { u32 eid; int drink; u64 at; bool toast; } Gesture;
static struct {
  MenuItem menu[MENU_MAX];
  int menu_n;
  bool asked;
  Gesture gestures[32];
  ListState list;
  bool picking, menu_mode;
} B;

/** Hand items only the bar serves: no idle sipping, they move when their holder acts. */
static bool bar_only(int drink) {
  static const int ids[] = {28, 29, 30, 31, 35, 40, 43, 44, 50, 53, 200, 201, 202};
  for (int i = 0; i < (int)COUNT(ids); i++) if (ids[i] == drink) return true;
  return false;
}
static bool smoke(int drink) { return drink == 201 || drink == 202; }
bool bar_room(void) { return !strcmp(W.slug, "afterhours"); }

static void command(const char *op, const char *extra) {
  char json[96];
  snprintf(json, sizeof json, "{\"op\":\"%s\"%s%s}", op, extra && extra[0] ? "," : "", extra ? extra : "");
  send_activity(json);
}

static void gesture(u32 eid, int drink, u64 now, bool toast) {
  int slot = 0;
  for (int i = 0; i < (int)COUNT(B.gestures); i++) {
    if (B.gestures[i].eid == eid) { slot = i; break; }
    if (B.gestures[i].at < B.gestures[slot].at) slot = i;
  }
  B.gestures[slot] = (Gesture){eid, drink, now, toast};
}

bool bar_receive(const char *text, u64 now) {
  json_t *j = json_loads(text, 0, NULL);
  const char *kind = json_string_value(json_object_get(j, "bar"));
  if (!kind) { json_decref(j); return false; }
  if (!strcmp(kind, "menu")) {
    B.menu_n = 0;
    size_t i; json_t *v;
    json_array_foreach(json_object_get(j, "items"), i, v) {
      if (B.menu_n >= MENU_MAX) break;
      MenuItem *m = &B.menu[B.menu_n++];
      m->id = (int)json_integer_value(json_object_get(v, "id"));
      m->price = (int)json_integer_value(json_object_get(v, "price"));
      m->smoke = json_is_true(json_object_get(v, "smoke"));
      utf8_copy(m->name, sizeof m->name, json_string_value(json_object_get(v, "name")));
      utf8_copy(m->about, sizeof m->about, json_string_value(json_object_get(v, "about")));
    }
  } else if (!strcmp(kind, "act")) {
    gesture((u32)json_integer_value(json_object_get(j, "entity")), (int)json_integer_value(json_object_get(j, "drink")), now, false);
  } else if (!strcmp(kind, "toast")) {
    size_t i; json_t *v;
    json_array_foreach(json_object_get(j, "entities"), i, v) {
      Ent *e = world_ent((u32)json_integer_value(v));
      if (e && e->drink) gesture(e->id, e->drink, now, true);
    }
  }
  json_decref(j);
  return true;
}

int bar_sip(const Ent *e, u64 now, bool moving) {
  int drink = now < e->drink_until ? e->drink : 0;
  if (!bar_only(drink)) return -1;
  if (moving) return 0;
  for (int i = 0; i < (int)COUNT(B.gestures); i++) {
    const Gesture *g = &B.gestures[i];
    if (g->eid == e->id && g->drink == drink) return now - g->at < (g->toast ? 1500u : smoke(drink) ? 1100u : 900u);
  }
  return 0;
}

bool bar_holding(void) {
  Ent *me = world_ent(W.me);
  return me && bar_only(me->drink) && now_ms() < me->drink_until;
}

// ---------------------------------------------------------------- "Press A"

bool bar_context(char *label, int cap, u64 now) {
  Ent *me = world_ent(W.me);
  if (!bar_room() || !me || ent_moving(me, now)) return false;
  bool near = false;
  for (int i = 0; i < MAX_ITEMS && !near; i++) {
    const Item *it = &W.items[i];
    near = it->used && it->def == BAR_COUNTER && abs(it->x - me->x) <= 2 && abs(it->y - me->y) <= 2;
  }
  for (int i = 0; i < MAX_ENTS && !near; i++) {
    const Ent *e = &W.ents[i];
    near = e->used && e->guest >= STAFF_BASE && !strncmp(e->name, "Bartender", 9) && abs(e->x - me->x) <= 2 && abs(e->y - me->y) <= 2;
  }
  if (!near) return false;
  snprintf(label, cap, "%s", bar_holding() ? TR("Bar · what you are holding") : TR("Order at the bar"));
  return true;
}

void bar_context_run(void) { ui_open(P_BAR); }

// ---------------------------------------------------------------- the window

static void order(const MenuItem *m) {
  char extra[24];
  snprintf(extra, sizeof extra, "\"item\":%d", m->id);
  command("bar_order", extra);
  B.menu_mode = false;
  ui_close();
}

static void actions(u64 now) {
  Ent *me = world_ent(W.me);
  int drink = me->drink;
  float x = IN_X + 6, y = IN_Y + 6;
  avatar_draw_held(drink, x + 18, y + 18, 2);
  const char *name = "Your drink";
  for (int i = 0; i < B.menu_n; i++) if (B.menu[i].id == drink) name = B.menu[i].name;
  sk_text(name, x + 42, y + 4, 11, SK_TEXT, 0, IN_W - 60);
  sk_text(smoke(drink) ? TR("A puffs. B closes.") : TR("A sips, X raises a toast. B closes."), x + 42, y + 20, 9, SK_DIM, 0, IN_W - 60);
  if (sk_button(x, y + 44, 96, smoke(drink) ? TR("Puff (A)") : TR("Sip (A)"), true) || K.a) command("bar_use", NULL);
  if (!smoke(drink) && (sk_button(x + 100, y + 44, 96, TR("Cheers! (X)"), true) || K.x)) command("bar_toast", NULL);
  if (sk_button(x + 200, y + 44, 90, TR("Put down"), true)) { command("bar_drop", NULL); ui_close(); return; }
  if (sk_button(x, y + 72, 150, B.picking ? TR("Hide the list") : TR("Hand over… (Y)"), true) || K.y) B.picking = !B.picking;
  if (sk_button(x + 156, y + 72, 134, TR("Order another"), true)) B.menu_mode = true;
  if (!B.picking) return;
  // Players within two tiles with an empty hand (the server checks it again).
  int row = 0;
  for (int i = 0; i < MAX_ENTS && row < 4; i++) {
    const Ent *e = &W.ents[i];
    if (!e->used || e->id == W.me || !e->guest || e->guest >= STAFF_BASE || abs(e->x - me->x) > 2 || abs(e->y - me->y) > 2) continue;
    if (sk_button(x, y + 104 + row * 24, 200, e->name, true)) {
      char extra[24]; snprintf(extra, sizeof extra, "\"target\":%u", (unsigned)e->id);
      command("bar_give", extra); B.picking = false;
    }
    row++;
  }
  if (!row) sk_text(TR("Stand next to someone to hand it over."), x, y + 108, 9, SK_DIM, 0, IN_W - 12);
}

void bar_draw(u64 now) {
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, TR("After Hours Bar"))) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  if (!bar_room()) { sk_paragraph(TR("The bar serves in After Hours Bar. Find it in Rooms."), IN_X + 6, IN_Y + 8, IN_W - 12, 10, SK_TEXT, 3); return; }
  if (bar_holding() && !B.menu_mode) { actions(now); return; }
  if (!B.asked) { B.asked = true; command("bar_menu", NULL); }
  if (!B.menu_n) { sk_text(TR("Loading the menu…"), IN_X + 6, IN_Y + 8, 10, SK_DIM, 0, IN_W - 12); return; }
  sk_text(TR("Vic serves at a free stool; you pay when served."), IN_X + 6, IN_Y + 3, 9, SK_DIM, 0, IN_W - 12);
  const float row = 30;
  if (K.dn && B.list.focus < B.menu_n - 1) B.list.focus++;
  if (K.up && B.list.focus > 0) B.list.focus--;
  int first = sk_list_begin(&B.list, IN_X, IN_Y + 16, IN_W, IN_H - 16, B.menu_n, row);
  for (int i = first; i < B.menu_n; i++) {
    float y = sk_list_row_y(i);
    if (y > IN_Y + IN_H) break;
    const MenuItem *m = &B.menu[i];
    bool focus = B.list.focus == i;
    fill_rect(IN_X + 2, y, IN_W - 4, row - 1, sk_rgb(focus ? SK_SELECT : i % 2 ? SK_ROW_ALT : SK_ROW));
    avatar_draw_held(m->id, IN_X + 16, y + row / 2, 1);
    sk_text(m->name, IN_X + 32, y + 2, 10.5f, SK_TEXT, 0, IN_W - 130);
    sk_text(TR(m->about), IN_X + 32, y + 15, 8.5f, SK_DIM, 0, IN_W - 130);
    char price[24]; snprintf(price, sizeof price, "%d %s", m->price, TR(m->price == 1 ? "credit" : "credits"));
    sk_text(price, IN_X + IN_W - 52, y + 8, 9.5f, SK_TEXT, 2, 70);
    if (sk_go(IN_X + IN_W - 48, y + 5, 40, 18, TR("Order"), (int)W.credits >= m->price) || (focus && K.a)) {
      if ((int)W.credits >= m->price) order(m);
      else ui_toast(TR("You do not have enough credits."), now);
    }
  }
  sk_list_end();
}
