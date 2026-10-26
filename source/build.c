#include "language.h"
// Build mode: place and move furniture with the Circle Pad / D-Pad,
// rotate with L/R, place with A and cancel with B. Mirrors the rules of
// renderer.canPlace() and ui.ts (startPlacing, placeAt) in the web edition.
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "scene.h"
#include "ui_int.h"
#include "world.h"

Ghost G;

static struct {
  u32 inv_id, moving;
  bool pending;
  u64 pending_since, repeat_at;
  int last_dir;
  int wall_slot;
} B;

bool build_active(void) { return G.on; }

static bool actor_at(int x, int y, bool pets_only) {
  u64 now = now_ms();
  for (int i = 0; i < MAX_ENTS; i++) {
    const Ent *e = &W.ents[i];
    if (!e->used) continue;
    if (!pets_only && e->x == x && e->y == y) return true;
    if (ent_moving(e, now) && (int)(e->fx + .5f) == x && (int)(e->fy + .5f) == y) return true;
  }
  return false;
}

/** Furniture covering the tile (wall items excluded). */
static int items_at(int x, int y, Item **out, int cap) {
  int n = 0;
  for (int i = 0; i < MAX_ITEMS && n < cap; i++) {
    Item *it = &W.items[i];
    if (!it->used) continue;
    const FurniDef *d = def_get(it->def);
    if (d && d->kind == KIND_WALL) continue;
    int w, h;
    def_size(it->def, it->rot, &w, &h);
    if (x >= it->x && y >= it->y && x < it->x + w && y < it->y + h) out[n++] = it;
  }
  return n;
}

// Builder :stackmode (server room.rs): 0 normal, 1 stack on any item, 2 flat. It lasts while
// the player stays in the room where it was typed, like on the server.
static struct { int mode; char slug[32]; } STACK;

void build_stack_command(const char *text) {
  if (strncmp(text, ":stackmode", 10) || (text[10] && text[10] != ' ') || !world_can_decorate()) return;
  const char *arg = text + 10;
  while (*arg == ' ') arg++;
  int mode = !strcmp(arg, "") ? STACK.mode == 0 && !strcmp(STACK.slug, W.slug) ? 1 : 0
           : !strcmp(arg, "0") || !strcmp(arg, "off") ? 0 : !strcmp(arg, "1") || !strcmp(arg, "on") ? 1
           : !strcmp(arg, "2") || !strcmp(arg, "flat") ? 2 : -1;
  if (mode < 0) return;
  STACK.mode = mode;
  snprintf(STACK.slug, sizeof STACK.slug, "%s", W.slug);
}

static int stack_mode(void) { return strcmp(STACK.slug, W.slug) ? 0 : STACK.mode; }

static bool can_place(Ghost *g) {
  const FurniDef *def = def_get(g->def);
  if (!world_can_decorate() || !def) return false;
  if (def->kind == KIND_WALL)
    return world_tile(g->x, g->y) && (g->rot % 2 == 0 ? !world_tile(g->x - 1, g->y) : !world_tile(g->x, g->y - 1)) && g->z >= 8 && g->z <= 110;
  const Item *original = g->ignore ? world_item(g->ignore) : NULL;
  if (original) {
    int ow, oh;
    def_size(original->def, original->rot, &ow, &oh);
    for (int dy = 0; dy < oh; dy++)
      for (int dx = 0; dx < ow; dx++) {
        int x = original->x + dx, y = original->y + dy;
        if ((def->kind != KIND_FLOOR || def->seat > 0) && actor_at(x, y, false)) return false;
        if (def->seat > 0 && !stack_mode()) {
          Item *list[16];
          int n = items_at(x, y, list, 16);
          for (int k = 0; k < n; k++) if (list[k]->id != original->id && list[k]->z >= original->z + def->seat) return false;
        }
      }
  }
  int w, h, height = -1;
  def_size(g->def, (u8)g->rot, &w, &h);
  for (int dy = 0; dy < h; dy++)
    for (int dx = 0; dx < w; dx++) {
      int x = g->x + dx, y = g->y + dy;
      if (!world_tile(x, y) || (x == W.door_x && y == W.door_y)) return false;
      if (actor_at(x, y, def->kind == KIND_FLOOR && def->seat == 0)) return false;
      int surface = 0, mode = stack_mode();
      Item *list[16];
      int n = mode == 2 ? 0 : items_at(x, y, list, 16);
      for (int k = 0; k < n; k++) {
        if (list[k]->id == g->ignore) continue;
        const FurniDef *support = def_get(list[k]->def);
        if (!support) return false;
        if (!mode && support->kind != KIND_FLOOR && !(support->flags & DEF_STACK)) return false;
        int top = list[k]->z + support->seat;
        if (top > surface) surface = top;
      }
      if (!mode && height >= 0 && height != surface) return false;
      if (surface > height) height = surface;
    }
  g->z = height < 0 ? 0 : height;
  return g->z < 256;
}

// ---------------------------------------------------------------- walls

typedef struct { int x, y, rot; } WallSlot;

/** Tiles next to a wall (like renderer.wallAt), in traversal order. */
static int wall_slots(WallSlot *out, int cap) {
  int n = 0;
  for (int y = 0; y < W.h; y++)
    for (int x = 0; x < W.w; x++) {
      if (!world_tile(x, y)) continue;
      for (int rot = 0; rot < 2 && n < cap; rot++) {
        if (world_tile(x - (rot == 0), y - (rot == 1))) continue;
        if (rot == 0 && x == W.door_x + 1 && y == W.door_y) continue;
        out[n++] = (WallSlot){x, y, rot};
      }
    }
  return n;
}

static void wall_apply(void) {
  static WallSlot slots[256];
  int n = wall_slots(slots, 256);
  if (!n) return;
  B.wall_slot = ((B.wall_slot % n) + n) % n;
  G.x = slots[B.wall_slot].x; G.y = slots[B.wall_slot].y; G.rot = slots[B.wall_slot].rot;
}

// ---------------------------------------------------------------- controls

void build_start(u16 def, u32 inv_id, u32 moving) {
  if (!world_can_decorate()) {
    ui_toast(moving ? TR("You can arrange furniture only in rooms you create.")
                    : TR("You can place furniture only in rooms you create. Open Rooms to create your own."), now_ms());
    return;
  }
  const FurniDef *d = def_get(def);
  memset(&B, 0, sizeof B);
  B.inv_id = moving ? moving : inv_id;
  B.moving = moving;
  memset(&G, 0, sizeof G);
  G.on = true;
  G.def = def;
  G.ignore = moving;
  G.wall = d && d->kind == KIND_WALL;
  const Item *it = moving ? world_item(moving) : NULL;
  Ent *me = world_ent(W.me);
  if (it) { G.x = it->x; G.y = it->y; G.rot = it->rot; G.z = it->z; }
  else if (me) { G.x = me->x; G.y = me->y; G.rot = def_rotation(def, 0); }
  if (G.wall) {
    // Start at the wall slot closest to the item or the player.
    static WallSlot slots[256];
    int n = wall_slots(slots, 256), best = 0, best_d = 1 << 30;
    for (int i = 0; i < n; i++) {
      int dd = (slots[i].x - G.x) * (slots[i].x - G.x) + (slots[i].y - G.y) * (slots[i].y - G.y);
      if (dd < best_d) { best_d = dd; best = i; }
    }
    B.wall_slot = best;
    wall_apply();
    if (!it) G.z = 60;
  }
  G.ok = can_place(&G);
  ui_close();
}

void build_cancel(void) {
  if (B.pending) return;
  memset(&G, 0, sizeof G);
}

static void place(void) {
  if (B.pending) return;
  G.ok = can_place(&G);
  if (!G.ok) { ui_toast(G.wall ? TR("Choose a free spot on the wall.") : TR("That spot is not free."), now_ms()); return; }
  B.pending = true;
  B.pending_since = now_ms();
  if (B.moving) send_move(B.moving, G.x, G.y, G.rot, G.z);
  else send_place(B.inv_id, G.x, G.y, G.rot, G.z);
}

void build_result(u32 item_id) {
  if (!G.on || !B.pending || item_id != B.inv_id) return;
  B.pending = false;
  memset(&G, 0, sizeof G);
}

void build_notice(void) { B.pending = false; }

void build_tap_tile(int x, int y) {
  if (!G.on || B.pending) return;
  if (G.wall) {
    static WallSlot slots[256];
    int n = wall_slots(slots, 256);
    for (int i = 0; i < n; i++) if (slots[i].x == x && slots[i].y == y) { B.wall_slot = i; wall_apply(); break; }
  } else { G.x = x; G.y = y; }
  G.ok = can_place(&G);
}

/** Screen direction of the pad → step on the isometric grid. */
static const s8 STEP[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};

static int pad_dir(u32 held) {
  circlePosition cp;
  hidCircleRead(&cp);
  int dx = cp.dx, dy = cp.dy;
  if (held & KEY_DUP) dy += 150;
  if (held & KEY_DDOWN) dy -= 150;
  if (held & KEY_DRIGHT) dx += 150;
  if (held & KEY_DLEFT) dx -= 150;
  if (dx * dx + dy * dy < 50 * 50) return -1;
  float a = atan2f((float)dx, (float)dy);
  return ((int)lroundf(a / ((float)M_PI / 4)) + 8) & 7;
}

bool build_input(u32 down, u32 held, u64 now) {
  if (!G.on) return false;
  if (B.pending && now - B.pending_since > 8000) B.pending = false;
  if (down & KEY_B) { build_cancel(); return true; }
  if (down & KEY_A) { place(); return true; }
  if (B.pending) return true;
  if (down & (KEY_L | KEY_R)) {
    if (G.wall) {
      // On the wall, L/R lower and raise the item.
      G.z += (down & KEY_R) ? 6 : -6;
      if (G.z < 8) G.z = 8;
      if (G.z > 110) G.z = 110;
    } else G.rot = def_rotation(G.def, (u8)((G.rot + 1) & 3));
    G.ok = can_place(&G);
  }
  int dir = pad_dir(held);
  if (dir < 0) { B.last_dir = -1; return true; }
  // First step immediately, then repeat like a cursor.
  if (dir != B.last_dir || now >= B.repeat_at) {
    if (G.wall) {
      // Wall slots are walked in a line: right/down move forward.
      B.wall_slot += (dir >= 1 && dir <= 4) ? 1 : -1;
      wall_apply();
    } else {
      int nx = G.x + STEP[dir][0], ny = G.y + STEP[dir][1];
      if (nx >= 0 && ny >= 0 && nx < W.w && ny < W.h) { G.x = nx; G.y = ny; }
    }
    G.ok = can_place(&G);
    B.repeat_at = now + (dir == B.last_dir ? 110 : 320);
    B.last_dir = dir;
  }
  return true;
}

void build_draw_panel(u64 now) {
  (void)now;
  const FurniDef *d = def_get(G.def);
  sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, B.moving ? TR("Move item") : TR("Place item"));
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  fill_round(IN_X + 6, IN_Y + 6, 52, 52, 4, sk_rgb(SK_WHITE));
  gfx_furni_icon(G.def, IN_X + 32, IN_Y + 32, 46);
  sk_text(d ? d->name : TR("Item"), IN_X + 66, IN_Y + 8, 12, SK_TEXT, 0, IN_W - 72);
  const char *state = B.pending ? "Placing…" : G.ok ? "Free spot: press A to place." : G.wall ? "Choose a free spot on the wall." : "This spot is taken.";
  sk_text(state, IN_X + 66, IN_Y + 26, 10.5f, B.pending ? SK_DIM : G.ok ? SK_GO : SK_DANGER, 0, IN_W - 72);
  char pos[48];
  snprintf(pos, sizeof pos, "Tile %d, %d  ·  height %d", G.x, G.y, G.z);
  sk_text(pos, IN_X + 66, IN_Y + 42, 10, SK_DIM, 0, IN_W - 72);
  float y = IN_Y + 66;
  sk_paragraph(G.wall ? TR("Circle Pad / D-Pad: slide along the wall\nL / R: lower / raise\nA: place   B: cancel   Tap the map to jump")
                      : TR("Circle Pad / D-Pad: move\nL / R: rotate\nA: place   B: cancel   Tap the map to jump"),
               IN_X + 8, y, IN_W - 16, 10.5f, SK_TEXT, 4);
  float by = IN_Y + IN_H - 24, bw = (IN_W - 24) / 3;
  if (sk_button(IN_X + 6, by, bw, G.wall ? TR("Raise (R)") : TR("Rotate (L/R)"), !B.pending)) {
    if (G.wall) G.z = G.z + 6 > 110 ? 110 : G.z + 6;
    else G.rot = def_rotation(G.def, (u8)((G.rot + 1) & 3));
    G.ok = can_place(&G);
  }
  if (sk_button(IN_X + 12 + bw, by, bw, TR("Place (A)"), G.ok && !B.pending)) place();
  if (sk_button(IN_X + 18 + bw * 2, by, bw, TR("Cancel (B)"), !B.pending)) build_cancel();
}
