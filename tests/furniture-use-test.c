#include "../source/world.h"
#include "../source/proto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u64 clock_ms=1000000;
u64 now_ms(void){return clock_ms;}
void dbg_log(const char *format,...){(void)format;}
int rnd_range(int lo,int hi){return hi<=lo?lo:lo+rand()%(hi-lo);}
void on_room(void){}
void on_chat(u32 id,const char *text){(void)id;(void)text;}
void on_notice(const char *text){(void)text;}
void on_kicked(const char *text){(void)text;}
void on_transfer(const char *slug,const char *arrival){(void)slug;(void)arrival;}
void on_alert(const char *text){(void)text;}
void on_casino(const char *text){(void)text;}
void on_trade(const char *text){(void)text;}
void on_activity(const char *text){(void)text;}
void on_music(const char *text){(void)text;}
void on_snow(const u8 *data,int length){(void)data;(void)length;}
void on_social_changed(void){}
void on_name_changed(u32 id){(void)id;}
void on_item(u32 id){(void)id;}
void profile_load(char *name,int cap,u64 *look,u32 *credits){snprintf(name,cap,"Tester");*look=0;*credits=200;}
void profile_save(const char *name,u64 look,u32 credits){(void)name;(void)look;(void)credits;}
void sim_deliver(const u8 *data,int length){world_handle(data,length);}

/* Test fixtures can place arbitrary packed definitions in the real offline engine. */
#include "../source/sim.c"

static SItem *fixture(u16 definition){
  memset(&S,0,sizeof S);memset(&W,0,sizeof W);
  S.w=S.h=W.w=W.h=10;S.me=W.me=W.guest_id=W.owner=1;
  W.in_room=W.room_owner=true;
  memset(S.rows,'0',sizeof S.rows);memset(W.rows,'0',sizeof W.rows);
  S.model=&room_models[0];
  for(int i=0;i<room_model_count;i++)if(!strcmp(room_models[i].key,"model_a"))S.model=&room_models[i];
  S.ents[0]=(SEnt){.used=true,.id=1,.guest=1,.x=4,.y=5};
  W.ents[0]=(Ent){.used=true,.id=1,.guest=1,.x=4,.y=5};
  S.items[0]=(SItem){.used=true,.id=42,.def=definition,.x=5,.y=5,.rot=1};
  item_update(&S.items[0]);return &S.items[0];
}

static void ticks(int count){for(int i=0;i<count;i++){clock_ms+=500;tick(clock_ms);}}

int main(void){
  for(int i=0;i<4;i++){
    const u16 definitions[]={1099,1220,1085,1123};
    SItem *machine=fixture(definitions[i]);const FurniDef *definition=def_get(machine->def);
    request_drink(&S.ents[0],42,255);assert(!S.ents[0].drink);
    S.ents[0].flags=FLAG_DANCE;
    request_drink(&S.ents[0],42,definition->drink);
    assert(machine->state==1 && S.ents[0].drink==definition->drink);
    assert(!(S.ents[0].flags&FLAG_DANCE));
    assert(world_ent(1)->drink==definition->drink);
    ticks(definition->use_ticks);assert(machine->state==0);
  }
  SItem *machine=fixture(1331);
  request_drink(&S.ents[0],42,4);assert(machine->state==1 && !S.ents[0].drink);
  ticks(2);assert(!S.ents[0].drink);ticks(1);assert(S.ents[0].drink==4);ticks(3);assert(!machine->state);
  machine=fixture(1086);use_stateful(&S.ents[0],machine);assert(machine->state==1);use_stateful(&S.ents[0],machine);assert(!machine->state);
  machine=fixture(1719);request_drink(&S.ents[0],42,1);assert(S.ents[0].drink==1);
  machine=fixture(1131);use_stateful(&S.ents[0],machine);assert(machine->state==1);ticks(4);assert(!machine->state);
  machine=fixture(1047);use_stateful(&S.ents[0],machine);assert(machine->state==8);ticks(7);assert(machine->state<8);
  machine=fixture(1351);use_stateful(&S.ents[0],machine);assert(machine->state==31);ticks(12);assert(machine->state>=21 && machine->state<=30);
  puts("Furniture use: fridge, butler, sink, coffee, ice cream delay, toilet, tea, Lert, bottle, wheel and drink validation passed.");
}
