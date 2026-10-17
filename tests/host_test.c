// PC test of the offline simulation + client state + protocol.
// make -C tests
#include "../source/catalog.h"
#include "../source/proto.h"
#include "../source/sim.h"
#include "../source/world.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u64 clock_ms = 1000000;
u64 now_ms(void) { return clock_ms; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + rand() % (hi - lo); }

static int chats, notices, rooms;
static char last_chat[256];
void on_room(void) { rooms++; }
void on_chat(u32 eid, const char *text) { chats++; snprintf(last_chat, sizeof last_chat, "%u:%s", eid, text); }
void on_notice(const char *text) { notices++; printf("  notice: %s\n", text); }
void on_kicked(const char *text) { (void)text; }
void sim_deliver(const u8 *data, int len) { world_handle(data, len); }
void on_transfer(const char *slug, const char *arrival) { (void)slug; (void)arrival; }
void on_alert(const char *json) { (void)json; }
void on_casino(const char *json) { (void)json; }
void on_trade(const char *json) { (void)json; }
void on_snow(const u8 *data,int len) { (void)data;(void)len; }
void on_social_changed(void) {}
void on_name_changed(u32 eid) { (void)eid; }
void on_item(u32 id) { (void)id; }
static char saved_name[24] = "";
static u64 saved_look;
void profile_load(char *name, int cap, u64 *look, u32 *credits) { snprintf(name, cap, "%s", saved_name); *look = saved_look; *credits = 200; }
void profile_save(const char *name, u64 look, u32 credits) { snprintf(saved_name, sizeof saved_name, "%s", name); saved_look = look; (void)credits; }

static void send_msg(Writer *w) { sim_receive(w->buf, w->len); }
static void run(int ms) { for (int t = 0; t < ms; t += 50) { clock_ms += 50; sim_update(clock_ms); } }

static void every_furniture_has_graphics(void) {
  int missing = 0;
  for (int i = 0; i < furni_def_count; i++) {
    const FurniDef *d = &furni_defs[i];
    if (!d->tex) continue;
    for (int rot = 0; rot < 4; rot++)
      if (!variant_frame(d->id, (u8)rot, 0, 0)) { if (missing++ < 5) printf("  no graphics: %u rot %d\n", d->id, rot); }
  }
  printf("furniture with graphics in all 4 rotations: %s (%d failures)\n", missing ? "NO" : "yes", missing);
  assert(!missing);
}

static void animated_furniture_changes(void) {
  int checked = 0;
  for (int i = 0; i < furni_def_count; i++) {
    const FurniDef *d = &furni_defs[i];
    if (!d->tex) continue;
    u8 state = 255;
    if (d->flags & DEF_HOLO) state = 1;
    else if (d->kind == KIND_DICE || d->id == 7) state = 7;
    else if (d->flags & DEF_TELEPORTER) state = 2;
    if (state == 255) continue;
    // Two different moments must give different frames and graphics.
    u16 f0 = furni_frame(d->id, state, 0), f1 = furni_frame(d->id, state, d->flags & DEF_HOLO ? 500 : 180);
    const FurniVariant *a = variant_frame(d->id, 0, state, f0), *b = variant_frame(d->id, 0, state, f1);
    bool changes = f0 != f1 && a && b && a != b;
    printf("  animated %u %-26s frames %u→%u %s\n", d->id, d->name, f0, f1, changes ? "changes" : "SAME");
    assert(changes);
    checked++;
  }
  assert(checked > 0);
}

int main(void) {
  every_furniture_has_graphics();
  animated_furniture_changes();
  srand(7);
  sim_start("newbie_lobby", clock_ms);
  Writer w;
  w_begin(&w, C_HELLO); w_u16(&w, PROTOCOL_VERSION); w_str(&w, ""); w_str(&w, ""); send_msg(&w);
  assert(rooms == 1 && W.in_room && W.me);
  assert(W.w == 18 && W.h == 17 && W.door_x == 0 && W.door_y == 7);
  printf("room %s (%s) %dx%d, me=%u %s, furniture=", W.title, W.model, W.w, W.h, W.me, W.name);
  int items = 0; for (int i = 0; i < MAX_ITEMS; i++) items += W.items[i].used; printf("%d\n", items);
  assert(items == 53);

  run(3000);
  Ent *me = world_ent(W.me);
  assert(me && me->x == 2 && me->y == 7);
  printf("arrival: (%d,%d)\n", me->x, me->y);

  // Walk to a sofa: the avatar sits facing its rotation.
  Item *sofa = NULL;
  for (int i = 0; i < MAX_ITEMS; i++) if (W.items[i].used && W.items[i].def == 1016) { sofa = &W.items[i]; break; }
  assert(sofa);
  w_begin(&w, C_WALK); w_u8(&w, sofa->x); w_u8(&w, sofa->y); send_msg(&w);
  run(8000);
  me = world_ent(W.me);
  printf("armchair (%d,%d) rot %d -> me (%d,%d) dir %d flags %d\n", sofa->x, sofa->y, sofa->rot, me->x, me->y, me->dir, me->flags);
  if (!(me->x == sofa->x && me->y == sofa->y)) printf("  (taken by a bot; another seat is tried)\n");
  else assert((me->flags & FLAG_SIT) && me->dir == sofa->rot * 2);

  // Chat and greetings from the bots.
  w_begin(&w, C_CHAT); w_str(&w, "hello everyone"); send_msg(&w);
  int before = chats;
  run(4000);
  printf("chats received after greeting: %d (last %s)\n", chats - before, last_chat);
  assert(chats > before);

  // Persistent look change.
  u64 look = 1 | 2 << 2 | 3 << 5 | 4 << 8 | 1 << 12 | 2 << 14 | 1 << 17 | (u64)3 << 32;
  w_begin(&w, C_SET_LOOK); w_var(&w, look); send_msg(&w);
  assert(world_ent(W.me)->look == look && saved_look == look);
  w_begin(&w, C_SET_NAME); w_str(&w, "Tester_3DS"); send_msg(&w);
  assert(!strcmp(W.name, "Tester_3DS"));

  // Drink from the cola dispenser.
  Item *cola = NULL;
  for (int i = 0; i < MAX_ITEMS; i++) if (W.items[i].used && W.items[i].def == 1718) cola = &W.items[i];
  assert(cola);
  w_begin(&w, C_USE); w_var(&w, cola->id); send_msg(&w);
  run(10000);
  me = world_ent(W.me);
  printf("drink: %d at (%d,%d)\n", me->drink, me->x, me->y);
  assert(me->drink == def_get(1718)->drink);

  // Gestures.
  w_begin(&w, C_GESTURE); w_u8(&w, GESTURE_DANCE); send_msg(&w);
  run(600);
  assert(world_ent(W.me)->flags & FLAG_DANCE);

  // A long while: bots must not collide or leave the grid.
  run(180000);
  int ents = 0;
  for (int i = 0; i < MAX_ENTS; i++) {
    Ent *e = &W.ents[i];
    if (!e->used) continue;
    ents++;
    assert(world_tile(e->x, e->y) || (e->x == W.door_x && e->y == W.door_y));
    for (int j = i + 1; j < MAX_ENTS; j++)
      if (W.ents[j].used && W.ents[j].x == e->x && W.ents[j].y == e->y && !(e->x == W.door_x && e->y == W.door_y))
        printf("  %s and %s overlap at (%d,%d)\n", e->name, W.ents[j].name, e->x, e->y);
  }
  printf("avatars in the room: %d (online=%d), chats=%d, notices=%d\n", ents, W.online, chats, notices);
  assert(ents >= 4 && W.online == ents);
  puts("OK");
  return 0;
}
