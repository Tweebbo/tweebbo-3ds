// The player stands still on the arrival tile: bots must not get stuck at the door.
#include "../source/proto.h"
#include "../source/sim.h"
#include "../source/world.h"
#include <stdio.h>
#include <stdlib.h>
static u64 clock_ms = 1000000;
u64 now_ms(void) { return clock_ms; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + rand() % (hi - lo); }
void on_room(void) {}
void on_chat(u32 eid, const char *text) {}
void on_notice(const char *text) {}
void on_kicked(const char *text) { (void)text; }
void on_transfer(const char *slug, const char *arrival) { (void)slug; (void)arrival; }
void on_alert(const char *json) { (void)json; }
void on_casino(const char *json) { (void)json; }
void on_snow(const u8 *data,int len) { (void)data;(void)len; }
void on_social_changed(void) {}
void on_name_changed(u32 eid) { (void)eid; }
void on_item(u32 id) { (void)id; }
void sim_deliver(const u8 *data, int len) { world_handle(data, len); }
void profile_load(char *name, int cap, u64 *look, u32 *credits) { name[0] = 0; *look = 0; *credits = 200; }
void profile_save(const char *name, u64 look, u32 credits) {}
int main(void) {
  srand(3);
  sim_start("newbie_lobby", clock_ms);
  Writer w; w_begin(&w, C_HELLO); w_u16(&w, PROTOCOL_VERSION); w_str(&w, ""); w_str(&w, ""); sim_receive(w.buf, w.len);
  u64 since[MAX_ENTS] = {0}; u32 who[MAX_ENTS] = {0};
  for (int t = 0; t < 120000; t += 50) {
    clock_ms += 50; sim_update(clock_ms);
    for (int i = 0; i < MAX_ENTS; i++) {
      Ent *e = &W.ents[i];
      bool door = e->used && e->x == W.door_x && e->y == W.door_y;
      if (!door || who[i] != e->id) { who[i] = door ? e->id : 0; since[i] = clock_ms; continue; }
      if (clock_ms - since[i] > 4000) { for (int k = 0; k < MAX_ENTS; k++) if (W.ents[k].used) printf("  %s (%d,%d) next=(%d,%d)\n", W.ents[k].name, W.ents[k].x, W.ents[k].y, W.ents[k].next_x, W.ents[k].next_y); printf("FAIL: %s has been at the door for 4 s (t=%d)\n", e->name, t); return 1; }
    }
  }
  printf("online=%d\nOK\n", W.online);
  return 0;
}
