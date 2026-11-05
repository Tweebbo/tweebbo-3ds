#include "world.h"
#include "proto.h"
#include "catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef MOTION_ALLOCATION_AUDIT
static size_t allocations;
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {allocations++;return __real_malloc(size);}
#endif
static u64 clock_ms=1000;
u64 now_ms(void) { return clock_ms; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
void on_room(void) {}
void on_chat(u32 id, const char *s) { (void)id; (void)s; }
static unsigned styled_chats;static s8 last_width;static u8 last_flags;static size_t styled_length;
void on_styled_chat(u32 id,const char *text,s8 width,u8 flags){assert(id==901);styled_chats++;last_width=width;last_flags=flags;styled_length=strlen(text);}
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

static u8 *packet_file(const char *path,int *size) {
  FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0&&n<=1024*1024);rewind(f);
  u8 *bytes=malloc((size_t)n);assert(bytes);assert(fread(bytes,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));*size=(int)n;return bytes;
}
static int replay_unit(int argc,char **argv) {
  assert(argc==6);int size;u8 *room=packet_file(argv[1],&size);assert(room[0]==S_ROOM);world_handle(room,size);free(room);
  u32 ids[3];for(int i=0;i<3;i++){ids[i]=(u32)strtoul(argv[i+3],NULL,10);Item *it=world_item(ids[i]);assert(it&&it->x==2&&it->y==6&&it->z==i*6);}
  u8 *slide=packet_file(argv[2],&size);assert(slide[0]==S_SLIDE);
#ifdef MOTION_ALLOCATION_AUDIT
  allocations=0;
#endif
  world_handle(slide,size);
#ifdef MOTION_ALLOCATION_AUDIT
  assert(allocations==0);
#endif
  for(int i=0;i<3;i++){
    Item *it=world_item(ids[i]);assert(it&&it->x==3&&it->y==6&&it->z==i*6&&it->slide_ms==1500);
    float x,y,z;item_pos(it,1750,&x,&y,&z);assert(fabsf(x-2.5f)<0.001f&&y==6&&z==i*6);
    item_pos(it,2500,&x,&y,&z);assert(x==3&&y==6&&z==i*6);
  }
  free(slide);free(W.owners_json);
  puts("PASS native server-packet replay: three original stack items retain IDs/heights, interpolate together at 1500 ms and decode with zero malloc calls.");return 0;
}
static int replay_flashes(int argc,char **argv) {
  assert(argc==5);int size;u8 *room=packet_file(argv[2],&size);assert(room[0]==S_ROOM);world_handle(room,size);free(room);
  u32 id=(u32)strtoul(argv[4],NULL,10);assert(world_item(id)&&world_item(id)->state==0);
  u8 *trace=packet_file(argv[3],&size);int at=0;
  static const u8 states[]={1,1,0,0,1,1,0},xs[]={1,2,2,3,3,4,4};
  for(int i=0;i<7;i++){
    assert(size-at>=4);u32 n=(u32)trace[at]|(u32)trace[at+1]<<8|(u32)trace[at+2]<<16|(u32)trace[at+3]<<24;at+=4;
    assert(n>0&&n<=(u32)(size-at));assert(trace[at]==(i%2==0?S_ITEM_UPDATE:S_SLIDE));
#ifdef MOTION_ALLOCATION_AUDIT
    allocations=0;
#endif
    world_handle(trace+at,(int)n);
#ifdef MOTION_ALLOCATION_AUDIT
    assert(allocations==0);
#endif
    Item *it=world_item(id);assert(it&&it->state==states[i]&&it->x==xs[i]&&it->y==1);at+=(int)n;
  }
  assert(at==size);free(trace);free(W.owners_json);
  puts("PASS native flash-packet replay: seven actual held state/movement updates preserve one box identity and decode with zero malloc calls.");return 0;
}
static int replay_precise(int argc,char **argv) {
  assert(argc==8);int size;u8 *room=packet_file(argv[2],&size);assert(room[0]==S_ROOM_PRECISE);world_handle(room,size);free(room);
  u32 mover=(u32)strtoul(argv[4],NULL,10),owned=(u32)strtoul(argv[5],NULL,10),actor=(u32)strtoul(argv[6],NULL,10);
  u16 top=(u16)strtoul(argv[7],NULL,10);assert(world_item(mover)&&item_height100(world_item(mover))==1032);
  u8 *trace=packet_file(argv[3],&size);int at=0;unsigned count=0,seen=1;
  while(at<size){
    assert(size-at>=4);u32 n=(u32)trace[at]|(u32)trace[at+1]<<8|(u32)trace[at+2]<<16|(u32)trace[at+3]<<24;at+=4;
    assert(n>0&&n<=(u32)(size-at));u8 kind=trace[at];if(kind>=151&&kind<=156)seen|=1u<<(kind-151);
#ifdef MOTION_ALLOCATION_AUDIT
    allocations=0;
#endif
    world_handle(trace+at,(int)n);
#ifdef MOTION_ALLOCATION_AUDIT
    assert(allocations==0);
#endif
    if(kind==S_ITEM_ADD_PRECISE)assert(world_item(owned)&&item_height100(world_item(owned))==top*100u+32);
    if(kind==S_ITEM_UPDATE_PRECISE)assert(world_item(owned)&&item_height100(world_item(owned))==top*100u+64);
    at+=(int)n;count++;
  }
  assert(seen==63&&at==size);const Item *m=world_item(mover),*o=world_item(owned);const Ent *e=world_ent(actor);
  assert(m&&m->x==3&&m->y==5&&item_height100(m)==1032&&m->slide_ms==1500);
  assert(o&&o->x==4&&o->y==3&&item_height100(o)==top*100u+64);
  assert(e&&e->x==3&&e->y==4&&e->z_fine==0);
  free(trace);free(W.owners_json);
  printf("PASS native precise-height replay: all six actual server packet families, %u updates, exact same-pixel placement changes and Wired slides; zero malloc calls.\n",count);return 0;
}
static void height_item(Writer *w,u32 id,u8 x,u8 rot,u16 z,u8 fine) {
  w_var(w,id);w_u16(w,61001);w_u8(w,x|(fine&64));w_u8(w,7);w_u8(w,rot|((fine&63)<<2));w_u8(w,1);w_var(w,9);w_u16(w,z);
}
static void height_entity(Writer *w,u32 id,u8 x,u8 dir,u16 z,u8 fine,bool delta) {
  w_var(w,id);if(!delta)w_str(w,"Height fixture");w_u8(w,x|((fine>>5)<<6));w_u8(w,7);w_u8(w,dir|((fine&31)<<3));w_u8(w,FLAG_WAVE);
  if(delta){w_u8(w,255);w_u8(w,255);}else{w_var(w,(1ull<<41)|1234);}
  w_u16(w,z);if(!delta)w_var(w,9);
}
static void height_room(Writer *w,u8 fine) {
  char rows[520];for(int i=0;i<519;i++)rows[i]=i%65==64?'\n':'0';rows[519]=0;
  w_begin(w,S_ROOM_PRECISE);w_str(w,"height");w_str(w,"Height fixture");w_str(w,"model_a");w_u8(w,64);w_u8(w,8);w_str(w,rows);w_u8(w,0);w_u8(w,7);w_var(w,9);w_str(w,"Owner");
  w_var(w,1);height_item(w,900,63,3,0,fine);w_var(w,1);height_entity(w,901,63,7,1,64,false);
}
static void placement_height_cases(void) {
  u32 height;memset(W.items,0,sizeof W.items);W.w=W.h=8;memset(W.rows,'0',64);W.door_x=0;W.door_y=3;
  for(int fine=0;fine<100;fine++){
    W.items[0]=(Item){.used=true,.id=300,.def=6,.x=3,.y=3,.z=5,.z_fine=(u8)fine};
    assert(world_placement_height100(6,3,3,0,0,0,&height)&&height==1100u+fine);
  }
  assert(world_placement_height100(6,3,3,0,300,0,&height)&&height==0);
  assert(world_placement_height100(6,3,3,0,0,2,&height)&&height==0);
  W.items[0].z=255;W.items[0].z_fine=99;assert(!world_placement_height100(6,3,3,0,0,0,&height));
  for(int i=0;i<17;i++)W.items[i]=(Item){.used=true,.id=300u+i,.def=6,.x=3,.y=3};
  W.items[17]=(Item){.used=true,.id=400,.def=4,.x=3,.y=3};
  const FurniDef *block=def_get(4);assert(block&&!(block->flags&DEF_STACK)&&block->kind!=KIND_FLOOR);
  assert(!world_placement_height100(6,3,3,0,0,0,&height));
  assert(world_placement_height100(6,3,3,0,0,1,&height));
  const FurniDef *wide=NULL;
  for(int i=0;i<furni_def_count;i++)if(furni_defs[i].kind!=KIND_WALL&&furni_defs[i].w>1&&furni_defs[i].w<=4&&furni_defs[i].h<=4){wide=&furni_defs[i];break;}
  assert(wide);int width,depth;def_size(wide->id,0,&width,&depth);assert(width*depth>1&&width*depth<MAX_ITEMS);
  memset(W.items,0,sizeof W.items);int slot=0;
  for(int y=1;y<1+depth;y++)for(int x=1;x<1+width;x++){
    W.items[slot]=(Item){.used=true,.id=500u+(u32)slot,.def=6,.x=(u8)x,.y=(u8)y};slot++;
  }
  assert(world_placement_height100(wide->id,1,1,0,0,0,&height)&&height==600);
  W.items[slot-1].z_fine=1;assert(!world_placement_height100(wide->id,1,1,0,0,0,&height));
  assert(world_placement_height100(wide->id,1,1,0,0,1,&height)&&height==601);
  memset(W.items,0,sizeof W.items);
  W.items[0]=(Item){.used=true,.id=300,.def=6,.x=3,.y=3,.z_fine=32};
  W.items[1]=(Item){.used=true,.id=301,.def=6,.x=3,.y=3,.z=6,.z_fine=31};
  assert(!world_supports_items(&W.items[0]));W.items[1].z_fine=32;assert(world_supports_items(&W.items[0]));
  W.items[1].x=4;assert(!world_supports_items(&W.items[0]));
  assert(!world_placement_height100(6,0,3,0,0,0,&height));assert(!world_placement_height100(6,8,3,0,0,0,&height));
  puts("Native placement: exact support fractions, ignored item, flat mode, height limit, blockers beyond sixteen items and precise support thresholds passed.");
}
static void magic_placement_cases(void) {
  int restricted=0;
  for(int i=0;i<cat_offer_count;i++){
    const CatOffer *o=&cat_offers[i];
    bool expected=o->def==40498||o->def==40499||o->def==40502||o->def==40503||o->def==40504;
    assert((o->requires_club!=0)==expected);restricted+=o->requires_club!=0;
  }
  assert(restricted==5);
  u32 previous_me=W.me;Ent previous_ent=W.ents[0];W.me=UINT32_MAX;
  W.ents[0]=(Ent){.used=true,.id=UINT32_MAX};assert(!world_club_member());
  W.ents[0].flags=FLAG_CLUB;assert(world_club_member());
  W.ents[0].flags=0;assert(!world_club_member());
  W.ents[0].id=123;assert(!world_club_member());W.me=previous_me;W.ents[0]=previous_ent;
  puts("Native Club catalogue: five source restrictions, active-player membership, expiry and unchanged offer padding passed.");
  const FurniDef *stack=NULL,*walk=NULL,*large=NULL;
  for(int i=0;i<furni_def_count;i++){
    const FurniDef *d=&furni_defs[i];
    if(def_height_helper(d)==1&&d->w==1&&d->h==1)stack=d;
    if(def_height_helper(d)==2&&d->w==1&&d->h==1)walk=d;
    if(def_height_helper(d)==1&&d->w==8&&d->h==8)large=d;
  }
  assert(stack&&walk&&large);WorldPlacement plan;
  memset(W.items,0,sizeof W.items);W.w=W.h=16;memset(W.rows,'0',256);W.door_x=0;W.door_y=3;
  W.items[0]=(Item){.used=true,.id=1,.def=4,.x=3,.y=3,.z=50};
  W.items[1]=(Item){.used=true,.id=2,.def=stack->id,.x=3,.y=3,.z_fine=32};
  assert(world_placement(6,3,3,0,0,0,&plan)&&plan.height100==32&&plan.helper_cells==1);
  assert(!world_supports_items(&W.items[1]));
  W.items[2]=(Item){.used=true,.id=3,.def=walk->id,.x=3,.y=3,.z=1,.z_fine=28};
  assert(world_item_at(3,3)->id==3);
  assert(world_placement(6,3,3,0,0,0,&plan)&&plan.height100==128);
  W.items[1].z=100;assert(world_item_at(3,3)->id==3);
  assert(world_placement(6,3,3,0,0,0,&plan)&&plan.height100==10032);
  W.items[1].used=false;W.rows[3*W.w+3]='x';
  assert(!world_tile(3,3)&&world_walkable_tile(3,3));
  assert(world_placement(6,3,3,0,0,0,&plan)&&plan.height100==128);
  W.items[2].used=false;assert(!world_walkable_tile(3,3));
  assert(!world_placement(6,3,3,0,0,0,&plan));
  assert(world_placement(stack->id,3,3,0,0,0,&plan)&&plan.height100==8200);
  W.items[0].z=2000;assert(world_placement(stack->id,3,3,0,0,0,&plan)&&plan.height100==128000);
  memset(W.items,0,sizeof W.items);memset(W.rows,'x',256);
  assert(world_placement(large->id,4,4,0,0,0,&plan)&&plan.height100==0);
  W.items[0]=(Item){.used=true,.id=1,.def=large->id,.x=4,.y=4,.z=2,.z_fine=56};
  assert(world_placement(large->id,4,4,0,0,0,&plan)&&plan.height100==256&&plan.helper_cells==UINT64_MAX);
  assert(world_placement(6,11,11,0,0,0,&plan)&&plan.height100==256);
  assert(!world_placement(large->id,9,4,0,0,0,&plan));
  assert(!world_placement(large->id,0,0,0,0,0,&plan));
  W.rows[11*W.w+11]='2';
  assert(world_placement(large->id,4,4,0,1,0,&plan)&&plan.height100==3200);
  memset(W.items,0,sizeof W.items);memset(W.rows,'0',256);W.rows[3*W.w+3]='2';
  assert(world_placement(6,3,3,0,0,0,&plan)&&plan.height100==3200);
  assert(!world_walkable_tile(-1,3)&&!world_walkable_tile(16,3));
  puts("Native helpers: exact override, walking priority, void/bounds/door rules, full 64-cell masks, raised floors and source height cap passed.");
}
static void precise_height_cases(void) {
  Writer w;clock_ms=10000;height_room(&w,32);
#ifdef MOTION_ALLOCATION_AUDIT
  size_t before_allocations=allocations;
#endif
  world_handle(w.buf,w.len);assert(W.w==64&&W.h==8&&world_item(900)->z_fine==32&&fabsf(item_z(world_item(900))-.32f)<.00001f);
  assert(world_ent(901)->look==((1ull<<41)|1234)&&world_ent(901)->z_fine==64&&fabsf(ent_height(world_ent(901),10000)-1.64f)<.00001f);
  for(int x=0;x<64;x++)for(int fine=0;fine<100;fine++)for(int rot=0;rot<4;rot++){
    w_begin(&w,S_ITEM_UPDATE_PRECISE);height_item(&w,900,(u8)x,(u8)rot,0,(u8)fine);world_handle(w.buf,w.len);
    const Item *it=world_item(900);assert(it->x==x&&it->rot==rot&&it->z_fine==fine&&item_height100(it)==(u32)fine);
  }
  height_room(&w,32);world_handle(w.buf,w.len);
  w_begin(&w,S_SLIDE_PRECISE);w_var(&w,1);height_item(&w,900,62,3,5,96);w_u16(&w,1500);world_handle(w.buf,w.len);
  float x,y,z;item_pos(world_item(900),10750,&x,&y,&z);assert(x==62.5f&&y==7&&fabsf(z-3.14f)<.00001f);
  Item saved;memcpy(&saved,world_item(900),sizeof saved);
  w_begin(&w,S_SLIDE_PRECISE);w_var(&w,2);height_item(&w,900,61,3,4,1);height_item(&w,902,60,0,4,100);w_u16(&w,500);world_handle(w.buf,w.len);assert(!memcmp(&saved,world_item(900),sizeof saved)&&!world_item(902));
  height_room(&w,100);world_handle(w.buf,w.len);assert(!memcmp(&saved,world_item(900),sizeof saved)&&!strcmp(W.slug,"height"));
  w_begin(&w,S_ENTITY_ADD_PRECISE);height_entity(&w,902,60,0,4,127,false);world_handle(w.buf,w.len);assert(!world_ent(902));
  w_begin(&w,S_DELTAS_PRECISE);w_var(&w,1);height_entity(&w,901,63,7,5,96,true);w_u16(&w,2000);world_handle(w.buf,w.len);assert(fabsf(ent_height(world_ent(901),11000)-3.8f)<.00001f);
  Ent saved_entity;memcpy(&saved_entity,world_ent(901),sizeof saved_entity);
  w_begin(&w,S_DELTAS_PRECISE);w_var(&w,2);height_entity(&w,901,62,0,4,1,true);height_entity(&w,902,60,0,4,100,true);world_handle(w.buf,w.len);assert(!memcmp(&saved_entity,world_ent(901),sizeof saved_entity));
  w_begin(&w,S_DELTAS_PRECISE);w_var(&w,1);height_entity(&w,901,63,2,5,32,true);w_u16(&w,0);world_handle(w.buf,w.len);assert(fabsf(ent_height(world_ent(901),10000)-5.32f)<.00001f);
  w_begin(&w,S_ITEM_ADD_PRECISE);height_item(&w,903,61,2,40,99);world_handle(w.buf,w.len);assert(world_item(903)->z_fine==99&&item_height100(world_item(903))==4099);
  w_begin(&w,S_ITEM_UPDATE);height_item(&w,903,61,2,41,0);world_handle(w.buf,w.len);assert(world_item(903)->z_fine==0&&item_z(world_item(903))==41);
#ifdef MOTION_ALLOCATION_AUDIT
  assert(allocations==before_allocations);
#endif
  assert(sizeof(Item)==56&&sizeof(Ent)==128);
  puts("Native precise heights: 25,600 packed coordinate/rotation cases, exact fractions, vertical interpolation, atomic invalid room/batches and legacy resets passed; zero malloc, Item 56 / Ent 128 bytes.");
}
static void move_style(Writer *w,u32 id,bool user,u8 style,s16 amount,u32 duration){w_begin(w,S_MOVE_STYLE);w_var(w,id);w_u8(w,user);w_u8(w,style);w_u16(w,(u16)amount);w_u16(w,0);w_var(w,duration);world_handle(w->buf,w->len);}
static void styled_movement(void){
  Writer w;height_room(&w,32);world_handle(w.buf,w.len);clock_ms=20000;
  Item *it=world_item(900);*it=(Item){.used=true,.id=900,.def=61001,.owner=1,.x=2,.y=7,.fx=2,.fy=7,.z_fine=32,.fz=.32f};
#ifdef MOTION_ALLOCATION_AUDIT
  size_t before=allocations;
#endif
  move_style(&w,900,false,7,80,100000);float x,y,z;item_pos(it,70000,&x,&y,&z);assert(x==2&&fabsf(z-.32f)<.0001f);
  w_begin(&w,S_SLIDE_PRECISE);w_var(&w,1);height_item(&w,900,6,0,0,32);w_u16(&w,2000);world_handle(w.buf,w.len);
  item_pos(it,45000,&x,&y,&z);assert(x==3&&fabsf(z-19.52f)<.0001f&&item_move_ms(it)==100000);
  clock_ms=45000;w_begin(&w,S_ITEM_UPDATE_PRECISE);height_item(&w,900,6,0,0,32);w.buf[8]=2;world_handle(w.buf,w.len);item_pos(it,45000,&x,&y,&z);assert(x==3&&fabsf(z-19.52f)<.0001f&&item_move_ms(it)==100000);
  move_style(&w,900,false,7,-80,1000);item_pos(it,45500,&x,&y,&z);assert(x==3&&fabsf(z-19.52f)<.0001f);
  w_begin(&w,S_SLIDE_PRECISE);w_var(&w,1);height_item(&w,900,7,0,10,64);w_u16(&w,1000);world_handle(w.buf,w.len);item_pos(it,45500,&x,&y,&z);assert(fabsf(x-5.004f)<.0001f&&z<15.08f&&item_move_ms(it)==1000);
  clock_ms=46000;w_begin(&w,S_SLIDE_PRECISE);w_var(&w,1);height_item(&w,900,8,0,10,64);w_u16(&w,500);world_handle(w.buf,w.len);assert(!it->curve_style&&item_move_ms(it)==500);item_pos(it,46250,&x,&y,&z);assert(x==7.5f&&fabsf(z-10.64f)<.0001f);
  Item saved=*it;move_style(&w,900,false,1,80,1000);assert(!memcmp(it,&saved,sizeof saved));move_style(&w,900,false,7,80,100001);assert(!memcmp(it,&saved,sizeof saved));
  Ent *e=world_ent(901);*e=(Ent){.used=true,.id=901,.x=2,.y=7,.fx=2,.fy=7,.z_fine=32,.fz=.32f};clock_ms=50000;move_style(&w,901,true,7,80,100000);
  w_begin(&w,S_DELTAS_PRECISE);w_var(&w,1);height_entity(&w,901,6,0,0,32,true);w_u16(&w,2000);world_handle(w.buf,w.len);ent_pos(e,75000,&x,&y);assert(x==3&&fabsf(ent_height(e,75000)-19.52f)<.0001f&&ent_move_ms(e)==100000);
  clock_ms=75000;move_style(&w,901,true,0,100,1);ent_pos(e,76000,&x,&y);assert(x==3);w_begin(&w,S_DELTAS);w_var(&w,1);height_entity(&w,901,7,0,0,0,true);w_u16(&w,500);world_handle(w.buf,w.len);assert(ent_move_ms(e)==1);ent_pos(e,75001,&x,&y);assert(x==7&&ent_height(e,75001)==0);
  clock_ms=76000;w_begin(&w,S_DELTAS);w_var(&w,1);height_entity(&w,901,8,0,0,0,true);w_u16(&w,500);world_handle(w.buf,w.len);assert(!e->curve_style&&ent_move_ms(e)==500);ent_pos(e,76250,&x,&y);assert(x==7.5f);
#ifdef MOTION_ALLOCATION_AUDIT
  assert(allocations==before);
#endif
  char unicode[2001];for(int i=0;i<500;i++)memcpy(unicode+i*4,"\xf0\x9f\x98\x80",4);unicode[2000]=0;
  static u8 chat_buffer[2100];w_begin_into(&w,S_STYLED_CHAT,chat_buffer,sizeof chat_buffer);w_var(&w,901);w_str(&w,unicode);w_u8(&w,255);w_u8(&w,3);world_handle(w.buf,w.len);assert(styled_chats==1&&last_width==-1&&last_flags==3&&styled_length==2000);
  w.buf[w.len-1]=4;world_handle(w.buf,w.len);assert(styled_chats==1);w.buf[w.len-1]=3;world_handle(w.buf,w.len-1);assert(styled_chats==1);
  w_begin(&w,S_ENTITY_NAME);w_var(&w,901);w_str(&w,"1234567890123456789012345678901\xe2\x82\xac");world_handle(w.buf,w.len);assert(strlen(e->name)==31);
  printf("Native styled movement: verified jump fixtures, signed strength, 100000ms/1ms, pending freeze/interruption, fractional altitude, state updates, atomic invalid metadata and legacy reset passed; World %zu bytes, Item %zu, Ent %zu, zero allocations.\n",sizeof(World),sizeof(Item),sizeof(Ent));
}
int main(int argc,char **argv) {
  if(argc>1&&!strcmp(argv[1],"--precise"))return replay_precise(argc,argv);
  if(argc>1&&!strcmp(argv[1],"--flashes"))return replay_flashes(argc,argv);
  if(argc>1)return replay_unit(argc,argv);
  styled_movement();memset(&W,0,sizeof W);clock_ms=1000;
  Writer w;
  for (unsigned i = 1; i <= 120; i++) {
    w_begin(&w, S_ENTITY_ADD); w_var(&w, i); w_str(&w, "Console player");
    w_u8(&w, 1); w_u8(&w, 1); w_u8(&w, 2); w_u8(&w, FLAG_3DS);
    w_var(&w, (1ull << 41) | 0x12345); w_u16(&w, 0); w_var(&w, i + 1000);
    world_handle(w.buf, w.len);
  }
  assert(world_ent(120) && world_ent(120)->look == ((1ull << 41) | 0x12345));
  w_begin(&w,S_EFFECT);w_var(&w,2);w_var(&w,120);w_u16(&w,38);w_var(&w,119);w_u16(&w,65535);world_handle(w.buf,w.len);
  assert(world_ent(120)->effect==38&&world_ent(119)->effect==65535);
  w_begin(&w,S_EFFECT);w_var(&w,1);w_var(&w,120);w_u16(&w,12);world_handle(w.buf,w.len-1);assert(world_ent(120)->effect==38);
  w_begin(&w,S_EFFECT);w_var(&w,1);w_var(&w,120);w_u16(&w,0);world_handle(w.buf,w.len);assert(world_ent(120)->effect==0);
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
  W.items[0]=(Item){.used=true,.id=100,.def=61001,.x=2,.y=3};
#ifdef MOTION_ALLOCATION_AUDIT
  size_t allocations_before=allocations;
#endif
  w_begin(&w,S_SLIDE);w_var(&w,1);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,3);w_u8(&w,3);w_u8(&w,0);w_u8(&w,3);w_var(&w,1);w_u16(&w,0);
  world_handle(w.buf,w.len);
  float x,y,z;item_pos(world_item(100),1250,&x,&y,&z);
  assert(x==2.5f && y==3 && z==0);
  clock_ms=1250;
  w_begin(&w,S_ITEM_UPDATE);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,3);w_u8(&w,3);w_u8(&w,0);w_u8(&w,2);w_var(&w,1);w_u16(&w,0);
  world_handle(w.buf,w.len);item_pos(world_item(100),1250,&x,&y,&z);
  assert(x==2.5f && world_item(100)->state==2);
  w_begin(&w,S_SLIDE);w_var(&w,1);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,4);w_u8(&w,3);w_u8(&w,0);w_u8(&w,2);w_var(&w,1);w_u16(&w,32);
  world_handle(w.buf,w.len);item_pos(world_item(100),1500,&x,&y,&z);
  assert(x==3.25f && y==3 && z==16);
  item_pos(world_item(100),2000,&x,&y,&z);assert(x==4 && z==32);
  clock_ms=2000;
  w_begin(&w,S_SLIDE);w_var(&w,1);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,6);w_u8(&w,3);w_u8(&w,0);w_u8(&w,2);w_var(&w,1);w_u16(&w,32);w_u16(&w,2000);
  world_handle(w.buf,w.len);item_pos(world_item(100),2500,&x,&y,&z);assert(x==4.5f&&z==32);
  assert(item_sliding(world_item(100),3999)&&!item_sliding(world_item(100),4000));
  clock_ms=2500;
  w_begin(&w,S_ITEM_UPDATE);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,6);w_u8(&w,3);w_u8(&w,0);w_u8(&w,1);w_var(&w,1);w_u16(&w,32);
  world_handle(w.buf,w.len);item_pos(world_item(100),2500,&x,&y,&z);assert(x==4.5f&&item_move_ms(world_item(100))==2000);
  w_begin(&w,S_SLIDE);w_var(&w,1);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,7);w_u8(&w,3);w_u8(&w,0);w_u8(&w,2);w_var(&w,1);w_u16(&w,32);w_u16(&w,50);
  world_handle(w.buf,w.len-1);assert(world_item(100)->x==6);
  w.buf[w.len-2]=49;world_handle(w.buf,w.len);assert(world_item(100)->x==6);
  w_begin(&w,S_SLIDE);w_var(&w,2);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,7);w_u8(&w,3);w_u8(&w,0);w_u8(&w,2);w_var(&w,1);w_u16(&w,32);w_var(&w,101);
  world_handle(w.buf,w.len);assert(world_item(100)->x==6&&!world_item(101));
  w_begin(&w,S_SLIDE);w_var(&w,MAX_ITEMS+1);world_handle(w.buf,w.len);assert(world_item(100)->x==6);
  w_begin(&w,S_SLIDE);w_var(&w,1);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,7);w_u8(&w,3);w_u8(&w,0);w_u8(&w,2);w_var(&w,1);w_u16(&w,32);
  world_handle(w.buf,w.len);item_pos(world_item(100),2750,&x,&y,&z);assert(x==5.75f&&item_move_ms(world_item(100))==500);
  w_begin(&w,S_ITEM_UPDATE);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,5);w_u8(&w,3);w_u8(&w,0);w_u8(&w,1);w_var(&w,1);w_u16(&w,32);
  world_handle(w.buf,w.len);item_pos(world_item(100),2750,&x,&y,&z);assert(x==5&&!item_sliding(world_item(100),2750));
  assert(sizeof(Item)<=56);
  assert(sizeof(Ent)<=128);
  Ent *actor=&W.ents[0];*actor=(Ent){.used=true,.id=1,.x=4,.y=3,.fx=4,.fy=3};clock_ms=3000;
  w_begin(&w,S_DELTAS);w_var(&w,1);w_var(&w,1);w_u8(&w,6);w_u8(&w,3);w_u8(&w,2);w_u8(&w,0);w_u8(&w,255);w_u8(&w,255);w_u16(&w,32);w_u16(&w,2000);
  world_handle(w.buf,w.len);ent_pos(actor,3500,&x,&y);assert(x==4.5f&&ent_height(actor,3500)==8&&ent_move_ms(actor)==2000);
  clock_ms=3500;w_begin(&w,S_DELTAS);w_var(&w,1);w_var(&w,1);w_u8(&w,6);w_u8(&w,3);w_u8(&w,4);w_u8(&w,FLAG_WAVE);w_u8(&w,255);w_u8(&w,255);w_u16(&w,32);
  world_handle(w.buf,w.len);ent_pos(actor,4000,&x,&y);assert(x==5&&ent_move_ms(actor)==2000);
  w_begin(&w,S_DELTAS);w_var(&w,1);w_var(&w,1);w_u8(&w,7);w_u8(&w,3);w_u8(&w,2);w_u8(&w,0);w_u8(&w,255);w_u8(&w,255);w_u16(&w,32);w_u16(&w,50);
  world_handle(w.buf,w.len-1);assert(actor->x==6);w.buf[w.len-2]=49;world_handle(w.buf,w.len);assert(actor->x==6);
  w_begin(&w,S_DELTAS);w_var(&w,2);w_var(&w,1);w_u8(&w,7);w_u8(&w,3);w_u8(&w,2);w_u8(&w,0);w_u8(&w,255);w_u8(&w,255);w_u16(&w,32);w_var(&w,2);
  world_handle(w.buf,w.len);assert(actor->x==6);
  w_begin(&w,S_DELTAS);w_var(&w,1);w_var(&w,1);w_u8(&w,7);w_u8(&w,3);w_u8(&w,2);w_u8(&w,0);w_u8(&w,255);w_u8(&w,255);w_u16(&w,64);w_u16(&w,0);
  world_handle(w.buf,w.len);ent_pos(actor,3500,&x,&y);assert(x==7&&ent_height(actor,3500)==64&&!ent_moving(actor,3500));
  w_begin(&w,S_DELTAS);w_var(&w,1);w_var(&w,1);w_u8(&w,5);w_u8(&w,3);w_u8(&w,6);w_u8(&w,0);w_u8(&w,255);w_u8(&w,255);w_u16(&w,64);
  world_handle(w.buf,w.len);ent_pos(actor,3750,&x,&y);assert(x==6&&ent_move_ms(actor)==500);
  printf("Native avatar motion: legacy/custom duration, vertical motion, state preservation, instant movement and atomic malformed-batch rejection passed; Ent %zu bytes, Item %zu bytes.\n",sizeof(Ent),sizeof(Item));
#ifdef MOTION_ALLOCATION_AUDIT
  assert(allocations==allocations_before);
  puts("Native movement allocation audit: zero malloc calls for valid, interrupted and malformed furniture/avatar batches.");
#endif
  puts("Native slides: legacy/configurable duration, interruption, state preservation, instant movement, atomic truncated/invalid rejection and unchanged item memory budget passed.");
  precise_height_cases();
  placement_height_cases();
  magic_placement_cases();
  free(W.owners_json);
  puts("Native protocol: 120 entities, 42-bit looks, finishes, founder identity, invitations and truncated game packets passed.");
}
