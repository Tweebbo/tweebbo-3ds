// PC test of the 3DS online client (net.c + ws.c + conn.c) against a
// real server. Only for local or test servers: it creates a guest.
//   make -C tests online SERVER=ws://127.0.0.1:8092
#include "../source/net.h"
#include "../source/proto.h"
#include "../source/world.h"
#include "../source/catalog.h"
#include "../source/furniture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

u64 now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return (u64)tv.tv_sec * 1000 + (u64)tv.tv_usec / 1000; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + rand() % (hi - lo); }

static int rooms, chats, transfers;
static char note_response[16385],expected_note[1001],kit_response[2048],wired_response[32769];
void on_furniture(const char *json){
  if(strstr(json,"\"op\":\"open\"")&&strstr(json,"\"kind\":\"note\""))snprintf(note_response,sizeof note_response,"%s",json);
  if(strstr(json,"\"op\":\"open\"")&&strstr(json,"\"kind\":\"football_gate\""))snprintf(kit_response,sizeof kit_response,"%s",json);
  if(strstr(json,"\"op\":\"wired_saved\""))snprintf(wired_response,sizeof wired_response,"%s",json);
}
static char last_chat[256], last_notice[256];
void on_room(void) { rooms++; }
void on_chat(u32 eid, const char *text) { chats++; snprintf(last_chat, sizeof last_chat, "%u:%s", eid, text); }
void on_notice(const char *text) { snprintf(last_notice, sizeof last_notice, "%s", text); printf("  notice: %s\n", text); }
void on_kicked(const char *text) { (void)text; }
void on_transfer(const char *slug, const char *arrival) { transfers++; net_goto(slug, arrival); }
void on_alert(const char *json) { printf("  alert: %s\n", json); }
void on_casino(const char *json) { (void)json; }
static char last_trade[2048];
void on_trade(const char *json) { snprintf(last_trade, sizeof last_trade, "%s", json); }
void on_activity(const char *json) { (void)json; }
void on_music(const char *json) { (void)json; }
void on_snow(const u8 *data,int len) { (void)data;(void)len; }
void on_social_changed(void) {}
void on_name_changed(u32 eid) { (void)eid; }
void on_item(u32 id) { (void)id; }
// The offline simulation is not used here.
void sim_start(const char *k, u64 n) { (void)k; (void)n; }
void sim_receive(const u8 *d, int l) { (void)d; (void)l; }
void sim_update(u64 n) { (void)n; }

static const char *server;
static u8 browser_build;
const char *tw_reception_slug(void) { return "recibidor"; }
static char store_dir[] = "/tmp/tweebbo-3ds-online-XXXXXX";
bool store_load(const char *key, char *out, int cap) {
  if (!strcmp(key, "server")) { snprintf(out, (size_t)cap, "%s", server); return true; }
  char path[256];
  snprintf(path, sizeof path, "%s/%s", store_dir, key);
  FILE *f = fopen(path, "r");
  if (!f) return false;
  int n = (int)fread(out, 1, (size_t)cap - 1, f);
  out[n > 0 ? n : 0] = 0;
  fclose(f);
  return true;
}
void store_save(const char *key, const char *value) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s", store_dir, key);
  FILE *f = fopen(path, "w");
  if (f) { fputs(value, f); fclose(f); }
}

static bool pump_until(bool (*cond)(void), int ms) {
  for (u64 end = now_ms() + (u64)ms; now_ms() < end;) {
    net_update(now_ms());
    if (cond()) return true;
    usleep(20000);
  }
  return false;
}
static bool in_room(void) { return W.in_room && world_ent(W.me); }
static bool got_chat(void) { return strstr(last_chat, "hello from the 3DS") != NULL; }
static bool paused(void) { return net_state() == NET_REPLACED; }
static bool back_online(void) { return net_state() == NET_ONLINE && in_room(); }
static u16 note_definition;
static bool got_note_item(void){for(int i=0;i<W.inv_n;i++)if(W.inv[i].def==note_definition)return true;return false;}
static bool got_note_response(void){return strstr(note_response,expected_note)!=NULL&&strstr(note_response,"#99DDFF")!=NULL;}
static void expansion_note_roundtrip(void){
  assert(!strncmp(server,"ws://127.0.0.1:",15));
  const CatOffer *offer=NULL;
  for(int i=0;i<cat_offer_count;i++){const FurniDef *d=def_get(cat_offers[i].def);if(cat_offers[i].available&&d&&d->feature==F_NOTE&&cat_offers[i].price<=W.credits){offer=&cat_offers[i];note_definition=d->id;break;}}
  assert(offer);send_buy(offer->id);assert(pump_until(got_note_item,5000));
  u32 item=0;for(int i=0;i<W.inv_n;i++)if(W.inv[i].def==note_definition){item=W.inv[i].id;break;}assert(item);
  for(int i=0;i<1000;i++)expected_note[i]=(char)('a'+i%26);
  expected_note[1000]=0;
  char request[1400];snprintf(request,sizeof request,"{\"op\":\"note_save\",\"item\":%u,\"text\":\"%s\",\"color\":\"#99DDFF\"}",item,expected_note);
  send_furniture(request);assert(pump_until(got_note_response,5000));
  printf("Native furniture round trip: purchased note, saved all 1,000 characters from the hand, received the exact text and color from the server.\n");
}
static u32 gate_item;
static u16 gate_definition;
static u64 wanted_look;
static int wanted_x=-1,wanted_y;
static bool got_gate_item(void){for(int i=0;i<W.inv_n;i++)if(W.inv[i].def==gate_definition){gate_item=W.inv[i].id;return true;}return false;}
static bool got_gate_placed(void){Item *item=world_item(gate_item);return item&&item->x==3&&item->y==3;}
static bool got_kit_response(void){return strstr(kit_response,"\"maleSet\":true")!=NULL;}
static bool got_wanted_look(void){Ent *e=world_ent(W.me);return e&&e->look==wanted_look&&(wanted_x<0||(e->x==wanted_x&&e->y==wanted_y));}
static void own_fixture_room(const char *title){
  char body[256];snprintf(body,sizeof body,"{\"title\":\"%s\",\"model\":\"cuarto\",\"token\":\"%s\"}",title,net_token());
  int job=net_http("POST","/api/rooms",body,true);HttpResult result={0};u64 end=now_ms()+10000;
  bool ready=false;while(!(ready=net_http_result(job,&result))&&now_ms()<end)usleep(20000);
  assert(ready&&result.status==200&&result.body);
  const char *start=strstr(result.body,"\"slug\":\"");assert(start);start+=8;
  char slug[9];memcpy(slug,start,8);slug[8]=0;assert(start[8]=='"');free(result.body);
  net_goto(slug,"");assert(pump_until(in_room,20000));assert(!strcmp(W.slug,slug));
}
static void expansion_kit_roundtrip(void){
  assert(!strncmp(server,"ws://127.0.0.1:",15));
  const CatOffer *offer=NULL;
  for(int i=0;i<cat_offer_count;i++){const FurniDef *d=def_get(cat_offers[i].def);if(cat_offers[i].available&&d&&d->feature==F_FOOTBALL_GATE){offer=&cat_offers[i];gate_definition=d->id;break;}}
  assert(offer);send_buy(offer->id);assert(pump_until(got_gate_item,5000));
  own_fixture_room("Native kit fixture");
  const u64 clothes=(0x7ffULL<<8)|(0x3ffULL<<22)|(0xfULL<<38);
  const u64 kit=(1ULL<<32)|0x45678,base=(1ULL<<32)|0x12345;
  wanted_x=-1;wanted_look=kit;send_look(kit);assert(pump_until(got_wanted_look,5000));
  char request[256];snprintf(request,sizeof request,"{\"op\":\"outfit_save\",\"item\":%u,\"name\":\"Native home kit\"}",gate_item);
  send_furniture(request);assert(pump_until(got_kit_response,5000));
  wanted_look=base;send_look(base);assert(pump_until(got_wanted_look,5000));
  send_place(gate_item,3,3,0,0);assert(pump_until(got_gate_placed,5000));
  wanted_x=3;wanted_y=3;wanted_look=(base&~clothes)|(kit&clothes);send_walk(3,3);assert(pump_until(got_wanted_look,12000));
  wanted_x=2;send_walk(2,3);assert(pump_until(got_wanted_look,5000));
  wanted_x=3;wanted_look=base;send_walk(3,3);assert(pump_until(got_wanted_look,5000));
  net_goto("recibidor","");assert(pump_until(in_room,20000));
  printf("Native kit round trip: bought and configured a gate from the hand, placed it, wore its clothes on entry and restored the base look on the next pass.\n");
}
static u16 wired_purchase_definition;
static u32 wired_purchase_after,wired_item;
static int wired_x,wired_y;
static bool got_wired_purchase(void){for(int i=0;i<W.inv_n;i++)if(W.inv[i].def==wired_purchase_definition&&W.inv[i].id>wired_purchase_after){wired_item=W.inv[i].id;return true;}return false;}
static bool got_wired_placed(void){Item *item=world_item(wired_item);return item&&item->x==wired_x&&item->y==wired_y;}
static bool got_wired_saved(void){char identity[40];snprintf(identity,sizeof identity,"\"item\":%u,",wired_item);return strstr(wired_response,identity)!=NULL;}
static bool got_variable_effect(void){Ent *e=world_ent(W.me);return e&&e->effect==11;}
static u32 wired_buy_place(const char *name,int x,int y){
  const CatOffer *offer=NULL;for(int i=0;i<cat_offer_count;i++)if(cat_offers[i].available&&!strcmp(cat_offers[i].name,name)){offer=&cat_offers[i];break;}
  assert(offer&&offer->price<=W.credits);wired_purchase_definition=offer->def;wired_purchase_after=0;
  for(int i=0;i<W.inv_n;i++)if(W.inv[i].id>wired_purchase_after)wired_purchase_after=W.inv[i].id;
  send_buy(offer->id);assert(pump_until(got_wired_purchase,5000));wired_x=x;wired_y=y;
  send_place(wired_item,(u8)x,(u8)y,0,0);assert(pump_until(got_wired_placed,5000));return wired_item;
}
static void wired_save(u32 item,const char *operation,const char *text,const char *params,u32 target){
  char request[1200],picks[32];if(target)snprintf(picks,sizeof picks,"%u",target);else picks[0]=0;
  snprintf(request,sizeof request,"{\"op\":\"wired_save\",\"item\":%u,\"config\":{\"operation\":\"%s\",\"text\":\"%s\",\"params\":[%s],\"targets\":[%s],\"users\":[],\"delay\":0}}",item,operation,text,params,picks);
  wired_item=item;wired_response[0]=0;send_furniture(request);assert(pump_until(got_wired_saved,5000));
}
static void expansion_variable_roundtrip(void){
  assert(!strncmp(server,"ws://127.0.0.1:",15));own_fixture_room("Native variable fixture");
  u32 variable=wired_buy_place("WIRED Variable: User Variable",0,0);
  wired_save(variable,"wf_var_user","native_total","1,10",0);
  u32 give=wired_buy_place("WIRED Effect: Give Variable",2,2);char params[120];
  snprintf(params,sizeof params,"9223372036854775807,0,%u,0",variable);wired_save(give,"wf_act_give_var","",params,0);
  assert(strstr(wired_response,"9223372036854775807"));
  u32 trigger=wired_buy_place("WIRED Trigger: User Clicks Furni",2,2);wired_save(trigger,"wf_trg_click_furni","","",variable);
  trigger=wired_buy_place("WIRED Trigger: User Clicks Furni",4,4);wired_save(trigger,"wf_trg_click_furni","","",give);
  u32 condition=wired_buy_place("WIRED Condition: Variable Value",4,4);
  snprintf(params,sizeof params,"9223372036854775807,2,%u,0,0",variable);wired_save(condition,"wf_cnd_var_val_match","",params,0);
  u32 freeze=wired_buy_place("WIRED Effect: Freeze User",4,4);wired_save(freeze,"wf_act_freeze","","11,0,0",0);
  send_use(variable);send_use(give);assert(pump_until(got_variable_effect,5000));
  net_goto("recibidor","");assert(pump_until(in_room,20000));
  printf("Native variable round trip: bought and placed six Wired boxes, saved a persistent player definition and exact signed maximum, clicked Give, passed its typed value condition and received the original X-Ray freeze effect.\n");
}
static void ignore(const u8 *data, int len, void *ctx) { (void)data; (void)len; (void)ctx; }

static u32 browser_id, browser_invite;
static u16 browser_console_effect;
static Duel browser_game;
static void browser_receive(const u8 *data, int len, void *ctx) {
  (void)ctx;
  Reader r; r_init(&r, data, len);
  int type = r_u8(&r);
  if (type == S_WELCOME) browser_id = (u32)r_var(&r);
  if(type==S_EFFECT){unsigned count=(unsigned)r_var(&r);for(unsigned i=0;i<count;i++){u32 id=(u32)r_var(&r);u16 effect=r_u16(&r);if(id==W.me)browser_console_effect=effect;}}
  if (type != S_GAME) return;
  int op = r_u8(&r);
  if (op == 1) browser_invite = (u32)r_var(&r);
  if (op != 2) return;
  Duel keep = W.duel; u32 revision = W.game_rev;
  world_handle(data, len); browser_game = W.duel;
  W.duel = keep; W.game_rev = revision;
}

static void browser_pump(Ws *ws, int ms) {
  for (u64 end = now_ms() + ms; now_ms() < end;) {
    assert(ws_poll(ws, 10, browser_receive, NULL)); net_update(now_ms());
  }
}

static void crossplay_games(Endpoint *ep) {
  Ws peer; Writer w; char err[128];
  assert(ws_connect(&peer, ep, "/ws/recibidor", 15000, err, sizeof err));
  w_begin(&w, C_HELLO); w_u16(&w, PROTOCOL_VERSION); w_str(&w, ""); w_str(&w, ""); w_u8(&w, 0); w_str(&w, ""); w_u8(&w, browser_build);
  assert(ws_send(&peer, w.buf, w.len)); browser_pump(&peer, 1500);
  assert(browser_id && world_ent(browser_id));
  assert(!(world_ent(browser_id)->flags & FLAG_3DS));
  w_begin(&w,C_EFFECT);w_u16(&w,11);assert(ws_send(&peer,w.buf,w.len));browser_pump(&peer,300);
  assert(world_ent(browser_id)->effect==11);
  w_begin(&w,C_EFFECT);w_u16(&w,12);net_send(w.buf,w.len);browser_pump(&peer,300);
  assert(world_ent(W.me)->effect==12&&browser_console_effect==12);
  w_begin(&w,C_EFFECT);w_u16(&w,73);net_send(w.buf,w.len);browser_pump(&peer,150);
  assert(world_ent(W.me)->effect==12&&browser_console_effect==12);
  w_begin(&w,C_EFFECT);w_u16(&w,0);net_send(w.buf,w.len);assert(ws_send(&peer,w.buf,w.len));browser_pump(&peer,300);
  assert(world_ent(W.me)->effect==0&&browser_console_effect==0&&world_ent(browser_id)->effect==0);
  puts("native/browser effects: protocol 147 round trip, both directions, invalid request and clear passed");
  for (int kind = 1; kind <= 3; kind++) {
    browser_invite = 0;
    send_game(1, browser_id, kind, 0, 0); browser_pump(&peer, 1000);
    assert(browser_invite == W.me);
    w_begin(&w, C_GAME); w_u8(&w, 2); w_var(&w, W.me); w_u8(&w, 1);
    assert(ws_send(&peer, w.buf, w.len)); browser_pump(&peer, 1000);
    assert(W.duel.id && W.duel.kind == kind && W.duel.move_count > 0);
    assert(!memcmp(W.duel.board, browser_game.board, 64));
    assert(W.duel.players[W.duel.turn] == W.me);
    send_game(3, W.duel.id, W.duel.moves[0][0], W.duel.moves[0][1], 5); browser_pump(&peer, 1000);
    assert(W.duel.turn == 1 && !memcmp(W.duel.board, browser_game.board, 64));
    w_begin(&w, C_GAME); w_u8(&w, 3); w_var(&w, browser_game.id);
    w_u8(&w, browser_game.moves[0][0]); w_u8(&w, browser_game.moves[0][1]); w_u8(&w, 5);
    assert(ws_send(&peer, w.buf, w.len)); browser_pump(&peer, 1000);
    assert(W.duel.turn == 0 && !memcmp(W.duel.board, browser_game.board, 64));
    send_game(4, W.duel.id, 0, 0, 0); browser_pump(&peer, 1000);
    assert(W.duel.result == 2 && browser_game.result == 2);
  }
  // Furniture trade (economy update): the browser invites the console, the console accepts,
  // then the browser cancels; both sides follow the server's state.
  last_trade[0] = 0;
  char json[96];
  snprintf(json, sizeof json, "{\"action\":\"invite\",\"target\":%u}", (unsigned)W.me);
  w_begin(&w, C_TRADE); w_str(&w, json); assert(ws_send(&peer, w.buf, w.len)); browser_pump(&peer, 1000);
  assert(strstr(last_trade, "\"stage\":\"invite\"") && "the console did not receive the trade invite");
  send_trade("{\"action\":\"accept\"}"); browser_pump(&peer, 1000);
  assert(strstr(last_trade, "\"open\":true") && "the trade did not open after the console accepted");
  w_begin(&w, C_TRADE); w_str(&w, "{\"action\":\"cancel\"}"); assert(ws_send(&peer, w.buf, w.len)); browser_pump(&peer, 1000);
  assert(strstr(last_trade, "\"stage\":\"cancelled\""));
  ws_close(&peer);
  puts("native/browser crossplay: Tic-tac-toe, Connect Four and Chess moves and resignation passed");
  puts("native/browser trade: invite, accept and cancel passed");
}

int main(int argc, char **argv) {
  setvbuf(stdout,NULL,_IOLBF,0);
  const char *build = getenv("TWEEBBO_WEB_BUILD");
  char *marker_end = NULL;
  long marker = build ? strtol(build, &marker_end, 10) : 0;
  if (!build || marker_end == build || *marker_end || marker < 1 || marker > 255) {
    fputs("Set TWEEBBO_WEB_BUILD to the test server's WEB_CLIENT_BUILD (make online reads the local web source).\n", stderr);
    return 1;
  }
  browser_build = (u8)marker;
  server = argc > 1 ? argv[1] : "ws://127.0.0.1:8092";
  if (!mkdtemp(store_dir)) return 1;
  srand((unsigned)now_ms());
  net_start(now_ms());
  bool entered=pump_until(in_room,20000);
  if(!entered)fprintf(stderr,"Native connection status: %s (state %d)\n",net_status(),net_state());
  assert(entered && "could not enter the lobby");
  Ent *me = world_ent(W.me);
  printf("online: room %s \"%s\" (%s), me=%s eid=%u guest=%u credits=%u inventory=%d\n", W.slug, W.title, W.model,
         W.name, W.me, W.guest_id, W.credits, W.inv_n);
  assert(me->flags & FLAG_3DS);
  printf("3DS flag on my entity: yes\n");

  char token[80];
  char key[160];
  Endpoint ep;
  endpoint_parse(server, &ep);
  if (ep.port == 80 || ep.port == 443) snprintf(key, sizeof key, "token-%s", ep.host);
  else snprintf(key, sizeof key, "token-%s-%d", ep.host, ep.port);
  assert(store_load(key, token, sizeof token) && token[0]);
  printf("token saved per server: yes (%zu characters)\n", strlen(token));

  send_chat("hello from the 3DS");
  assert(pump_until(got_chat, 5000));
  printf("chat round trip: %s\n", last_chat);

  // HTTP API: room list with authentication.
  int job = net_http("GET", "/api/rooms?scope=all", NULL, true);
  HttpResult res;
  u64 end = now_ms() + 10000;
  while (!net_http_result(job, &res) && now_ms() < end) usleep(20000);
  printf("  response %d: %.120s %s\n", res.status, res.body ? res.body : "", res.err);
  assert(res.status == 200 && res.body && (res.body[0] == '[' || res.body[0] == '{'));
  printf("GET /api/rooms: %d, %d bytes\n", res.status, res.len);
  free(res.body);

  // Change room and come back: the identity is kept.
  u32 guest = W.guest_id;
  net_goto("parque", "");
  rooms = 0;
  assert(pump_until(in_room, 20000));
  printf("park: %s (%s) %dx%d, same character: %s\n", W.slug, W.model, W.w, W.h, W.guest_id == guest ? "yes" : "no");
  assert(W.guest_id == guest);
  net_goto("recibidor", "");
  assert(pump_until(in_room, 20000));
  assert(W.guest_id == guest);

  if(getenv("TWEEBBO_EXPANSION_FEATURES")){expansion_note_roundtrip();expansion_kit_roundtrip();expansion_variable_roundtrip();}
  crossplay_games(&ep);

  // The SQLite development hotel has no cloud session takeover service.
  const char *local_smoke=getenv("TWEEBBO_LOCAL_SMOKE");
  if(local_smoke && !strcmp(local_smoke,"1")) {
    net_stop();
    puts("OK: local transport, identity, chat, room changes, browser games and trade; cloud takeover excluded.");
    return 0;
  }

  // Another client with the same character takes over (a browser tab after "Save /
  // restore character"). The console must pause instead of evicting it back forever.
  Ws other;
  char err[128];
  assert(ws_connect(&other, &ep, "/ws/recibidor", 15000, err, sizeof err));
  Writer w;
  w_begin(&w, C_HELLO); w_u16(&w, PROTOCOL_VERSION); w_str(&w, token); w_str(&w, ""); w_u8(&w, 0); w_str(&w, ""); w_u8(&w, browser_build);
  assert(ws_send(&other, w.buf, w.len));
  assert(pump_until(paused, 10000) && "the console was not told it was replaced");
  for (u64 stop = now_ms() + 4000; now_ms() < stop;) {
    net_update(now_ms());
    assert(ws_poll(&other, 20, ignore, NULL) && "the console evicted the other client");
    assert(net_state() == NET_REPLACED);
  }
  printf("takeover: console paused, other client kept the character\n");
  // "Play here" takes the character back; now the other client is released.
  net_resume();
  assert(pump_until(back_online, 20000));
  assert(W.guest_id == guest);
  bool released = false;
  for (u64 stop = now_ms() + 10000; now_ms() < stop && !released;) {
    net_update(now_ms());
    released = !ws_poll(&other, 20, ignore, NULL);
  }
  ws_close(&other);
  assert(released);
  printf("play here: console back online, other client released\n");
  net_stop();
  printf("OK\n");
  return 0;
}
