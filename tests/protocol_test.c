#include "world.h"
#include "proto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
u64 now_ms(void) { return 1000; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
void on_room(void) {}
void on_chat(u32 id, const char *s) { (void)id; (void)s; }
void on_notice(const char *s) { (void)s; }
void on_kicked(const char *text) { (void)text; }
void on_transfer(const char *s, const char *a) { (void)s; (void)a; }
void on_alert(const char *s) { (void)s; }
void on_casino(const char *s) { (void)s; }
void on_trade(const char *s) { (void)s; }
void on_activity(const char *s) { (void)s; }
void on_music(const char *s) { (void)s; }
void on_snow(const u8 *data,int len) { (void)data;(void)len; }
void on_social_changed(void) {}
void on_name_changed(u32 id) { (void)id; }
void on_item(u32 id) { (void)id; }

int main(void) {
  Writer w;
  for (unsigned i = 1; i <= 120; i++) {
    w_begin(&w, S_ENTITY_ADD); w_var(&w, i); w_str(&w, "Console player");
    w_u8(&w, 1); w_u8(&w, 1); w_u8(&w, 2); w_u8(&w, FLAG_3DS);
    w_var(&w, (1ull << 41) | 0x12345); w_u16(&w, 0); w_var(&w, i + 1000);
    world_handle(w.buf, w.len);
  }
  assert(world_ent(120) && world_ent(120)->look == ((1ull << 41) | 0x12345));
  w_begin(&w,S_TYPING);w_var(&w,120);w_u8(&w,1);world_handle(w.buf,w.len);
  assert(world_ent(120)->typing_until==9000);
  world_handle(w.buf,w.len-1);assert(world_ent(120)->typing_until==9000);
  w_begin(&w,S_TYPING);w_var(&w,120);w_u8(&w,0);world_handle(w.buf,w.len);
  assert(world_ent(120)->typing_until==0);
  w_begin(&w, S_FINISH); w_str(&w, "101"); w_str(&w, "201"); world_handle(w.buf, w.len);
  assert(!strcmp(W.wall, "101") && !strcmp(W.floor, "201"));
  w_begin(&w, S_OWNER_IDENTITY); w_str(&w, "{\"visible\":[{\"guest\":1001,\"presence\":true}],\"self\":null}"); world_handle(w.buf, w.len);
  assert(W.owners_rev == 1 && strstr(W.owners_json, "1001"));
  w_begin(&w, S_GAME); w_u8(&w, 1); w_var(&w, 22); w_u8(&w, 3); world_handle(w.buf, w.len);
  assert(W.invites[0].from == 22 && W.invites[0].kind == 3);
  w_begin(&w, S_GAME); w_u8(&w, 4); w_var(&w, 22); world_handle(w.buf, w.len);
  assert(!W.invites[0].from);
  w_begin(&w, S_GAME); w_u8(&w, 2); w_var(&w, 7); w_u8(&w, 1); w_var(&w, 1); w_var(&w, 2);
  for (int i = 0; i < 6; i++) w_u8(&w, 0);
  w_var(&w, 9); for (int i = 0; i < 9; i++) w_u8(&w, i == 4 ? 1 : 0);
  w_var(&w, 1); w_u8(&w, 0); w_u8(&w, 0);
  world_handle(w.buf, w.len);
  assert(W.duel.id == 7 && W.duel.board[4] == 1 && W.duel.move_count == 1);
  u32 rev = W.game_rev;
  world_handle(w.buf, w.len - 1);
  assert(W.game_rev == rev && W.duel.id == 7);
  // Cloud sends OWNER_IDENTITY before the initial ROOM snapshot.
  w_begin(&w, S_ROOM); w_str(&w, "fixture"); w_str(&w, "Fixture"); w_str(&w, "model_a");
  w_u8(&w, 2); w_u8(&w, 2); w_str(&w, "00\n00"); w_u8(&w, 0); w_u8(&w, 0);
  w_var(&w, 0); w_str(&w, ""); w_var(&w, 0); w_var(&w, 0);
  world_handle(w.buf, w.len);
  assert(W.owners_json && strstr(W.owners_json, "1001"));
  assert(!W.duel.id && W.game_rev > rev);
  free(W.owners_json);
  puts("Native protocol: 120 entities, 42-bit looks, finishes, founder identity, invitations and truncated game packets passed.");
}
