#include "language.h"
#include "language_js.h"
#include "snow.h"
#include "snow_audio.h"
#include "osk.h"
#include "ui_int.h"
#include "gfx.h"
#include "net.h"
#include "proto.h"
#include "world.h"
#include "scene.h"
#include "quickjs.h"
#include <jansson.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <malloc.h>

static JSRuntime *runtime;
static JSContext *context;
static JSValue api;
static bool active, textures, create_page;
static u64 deadline, snapshot_at, move_at;
static json_t *snapshot;
static int page, arena=1, teams=2, players=3, minutes=3, level=1, fill=2;
static char error[160];
typedef union { size_t size; max_align_t align; } Allocation;
static void *qmalloc(JSMallocState *s,size_t n) {
  if(!n||n>SIZE_MAX-sizeof(Allocation)||s->malloc_size+n+sizeof(Allocation)>s->malloc_limit){dbg_log("Snow allocation refused: %lu bytes; used %lu",(unsigned long)n,(unsigned long)s->malloc_size);return NULL;}
  Allocation *p=malloc(n+sizeof *p);if(!p){dbg_log("Snow heap allocation failed: %lu",(unsigned long)n);return NULL;}p->size=n;
  s->malloc_count++;s->malloc_size+=n+sizeof *p;return p+1;
}
static void qfree(JSMallocState *s,void *ptr) {
  if(!ptr)return;
  Allocation *p=(Allocation*)ptr-1;s->malloc_size-=p->size+sizeof *p;s->malloc_count--;free(p);
}
static void *qrealloc(JSMallocState *s,void *ptr,size_t n) {
  if(!ptr)return qmalloc(s,n);
  if(!n){qfree(s,ptr);return NULL;}
  Allocation *p=(Allocation*)ptr-1;size_t old=p->size;
  if(n>SIZE_MAX-sizeof *p||s->malloc_size-old+n>s->malloc_limit){dbg_log("Snow reallocation refused: %lu bytes; old %lu; used %lu",(unsigned long)n,(unsigned long)old,(unsigned long)s->malloc_size);return NULL;}
  p=realloc(p,n+sizeof *p);if(!p){dbg_log("Snow heap reallocation failed: %lu",(unsigned long)n);return NULL;}p->size=n;s->malloc_size=s->malloc_size-old+n;return p+1;
}
static size_t qusable(const void *ptr){return ptr?((const Allocation*)ptr-1)->size:0;}
static int interrupted(JSRuntime *rt,void *opaque){(void)rt;(void)opaque;return now_ms()>deadline;}
static double arg(JSContext *ctx,JSValueConst value){double n=0;JS_ToFloat64(ctx,&n,value);return isfinite(n)?n:0;}
static u32 color(double rgb,double alpha){unsigned c=(unsigned)rgb;return C2D_Color32(c>>16,(c>>8)&255,c&255,(u8)fmax(0,fmin(255,alpha*255)));}
static JSValue sprite(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<11)return JS_UNDEFINED;double a[11];for(int i=0;i<11;i++)a[i]=arg(ctx,argv[i]);
  gfx_snow_sprite(a[0],a[1],a[2],a[3],a[4],color(a[5],a[6]),a[7],a[8],a[9],a[10]);return JS_UNDEFINED;
}
static JSValue rectangle(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<7)return JS_UNDEFINED;double a[7];for(int i=0;i<7;i++)a[i]=arg(ctx,argv[i]);
  fill_round(a[0],a[1],a[2],a[3],a[4],color(a[5],a[6]));return JS_UNDEFINED;
}
static JSValue text(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<6)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  text_draw(s,arg(ctx,argv[1]),arg(ctx,argv[2]),arg(ctx,argv[3]),color(arg(ctx,argv[4]),1),arg(ctx,argv[5]));JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static JSValue measure(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<2)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  double w=text_width(s,arg(ctx,argv[1]));JS_FreeCString(ctx,s);return JS_NewFloat64(ctx,w);
}
static JSValue send_packet(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<1)return JS_UNDEFINED;size_t n=0;u8 *b=JS_GetArrayBuffer(ctx,&n,argv[0]);
  if(b&&n>1&&n<=1024&&b[0]==C_SNOW)net_send(b,n);
  return JS_UNDEFINED;
}
static JSValue sound(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<2)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  snow_audio_play(s,arg(ctx,argv[1])!=0);JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static JSValue stop_walk(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)ctx;(void)self;(void)argc;(void)argv;snow_audio_stop_walk();return JS_UNDEFINED;}
static void exception(void){
  JSValue e=JS_GetException(context);const char *s=JS_ToCString(context,e);
  dbg_log("Snow Storm: %s",s?s:"runtime error");
  JSValue stack=JS_GetPropertyStr(context,e,"stack");const char *trace=JS_ToCString(context,stack);
  if(trace)dbg_log("Snow Storm stack: %s",trace);
  JS_FreeCString(context,trace);JS_FreeValue(context,stack);
  JSMemoryUsage memory;JS_ComputeMemoryUsage(runtime,&memory);struct mallinfo heap=mallinfo();
  dbg_log("Snow Storm memory: JS %lu KB, heap used %lu KB, free %lu KB",(unsigned long)(memory.malloc_size/1024),(unsigned long)(heap.uordblks/1024),(unsigned long)(heap.fordblks/1024));
  snprintf(error,sizeof error,"%s",s&&strstr(s,"versions differ")?"Update Tweebbo to play Snow Storm with this hotel.":"Snow Storm stopped safely. Reopen it to reconnect to your match.");
  JS_FreeCString(context,s);JS_FreeValue(context,e);
}
static JSValue call(const char *name,int n,JSValue *args){
  if(!context)return JS_UNDEFINED;
  deadline=now_ms()+250;
  JSValue fn=JS_GetPropertyStr(context,api,name),result=JS_Call(context,fn,api,n,args);
  JS_FreeValue(context,fn);for(int i=0;i<n;i++)JS_FreeValue(context,args[i]);
  if(JS_IsException(result))exception();
  return result;
}
static void invoke(const char *name){JSValue r=call(name,0,NULL);if(context)JS_FreeValue(context,r);}
static void number_call(const char *name,double n){JSValue a=JS_NewFloat64(context,n),r=call(name,1,&a);JS_FreeValue(context,r);}
static bool init(void){
  static const JSMallocFunctions allocator={qmalloc,qfree,qrealloc,qusable};
  runtime=JS_NewRuntime2(&allocator,NULL);if(!runtime)return false;
  JS_SetMemoryLimit(runtime,12u*1024*1024);JS_SetMaxStackSize(runtime,128u*1024);JS_SetInterruptHandler(runtime,interrupted,NULL);
  JS_SetGCThreshold(runtime,512u*1024);
  deadline=now_ms()+5000;context=JS_NewContext(runtime);if(!context)return false;
  JSValue global=JS_GetGlobalObject(context);
  JS_SetPropertyStr(context,global,"nativeTranslate",JS_NewCFunction(context,tw_js_translate,"nativeTranslate",1));
  struct {const char *name;JSCFunction *fn;int argc;} fns[]={{"nativeSprite",sprite,11},{"nativeRect",rectangle,7},{"nativeText",text,6},{"nativeMeasure",measure,2},{"nativeSend",send_packet,1},{"nativeSound",sound,2},{"nativeStopWalk",stop_walk,0}};
  for(unsigned i=0;i<COUNT(fns);i++)JS_SetPropertyStr(context,global,fns[i].name,JS_NewCFunction(context,fns[i].fn,fns[i].name,fns[i].argc));
  { /* Guards against toolchain miscompiles of QuickJS (see Makefile): string iteration must end. */
    static const char check[]="[...'ab'].join('')+Array.from('cd').length";
    JSValue v=JS_Eval(context,check,sizeof check-1,"self-test",JS_EVAL_TYPE_GLOBAL);const char *t=JS_IsException(v)?NULL:JS_ToCString(context,v);
    bool ok=t&&!strcmp(t,"ab2");JS_FreeCString(context,t);if(JS_IsException(v))JS_FreeValue(context,JS_GetException(context));JS_FreeValue(context,v);
    if(!ok){dbg_log("Snow Storm: script engine self-test failed");JS_FreeValue(context,global);return false;}
  }
  snow_audio_init();
  FILE *f=fopen("romfs:/snow/game.js","rb");if(!f){JS_FreeValue(context,global);return false;}
  fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);
  char *script=len>0&&len<2*1024*1024?malloc(len+1):NULL;
  if(!script){fclose(f);JS_FreeValue(context,global);return false;}
  size_t read=fread(script,1,len,f);fclose(f);script[read]=0;
  JSValue result=JS_Eval(context,script,read,"snow/game.js",JS_EVAL_TYPE_GLOBAL);free(script);
  bool ok=!JS_IsException(result);if(!ok)exception();JS_FreeValue(context,result);
  api=JS_GetPropertyStr(context,global,"Snow");JS_FreeValue(context,global);return ok&&JS_IsObject(api);
}
void snow_open(void){
  ui_open(P_SNOW);active=true;error[0]=0;create_page=false;page=0;
}
void snow_close(bool notify){
  if(context){if(notify&&!error[0])invoke("leave");JS_FreeValue(context,api);JS_FreeContext(context);context=NULL;}
  if(runtime){JS_FreeRuntime(runtime);runtime=NULL;}
  snow_audio_exit();
  json_decref(snapshot);snapshot=NULL;active=false;snapshot_at=0;
}
void snow_receive(const u8 *data,int len){
  if(!active||!context||error[0]||len<=0||len>65536)return;
  JSValue a[]={JS_NewArrayBufferCopy(context,data,len),JS_NewInt64(context,W.me),JS_NewFloat64(context,now_ms())};
  JSValue r=call("receive",3,a);JS_FreeValue(context,r);snapshot_at=0;
}
void snow_frame(u64 now){
  if(!active){if(textures){gfx_snow_free();textures=false;scene_room_changed(now);}return;}
  if(error[0])return;
  if(!context){
    if(!init()){if(!error[0])snprintf(error,sizeof error,"%s",TR("Snow Storm could not start. Close it and try again."));return;}
    number_call("open",W.me);
  }
  if(!textures){gfx_suspend_room();if(!gfx_snow_load()){snprintf(error,sizeof error,"%s",TR("Not enough graphics memory for Snow Storm. Close it and try again."));textures=true;return;}textures=true;}
  number_call("tick",now);
  if(now-snapshot_at>=60){
    JSValue a=JS_NewFloat64(context,now),r=call("status",1,&a);const char *s=JS_ToCString(context,r);
    json_t *next=s?json_loads(s,0,NULL):NULL;JS_FreeCString(context,s);JS_FreeValue(context,r);
    if(next){json_decref(snapshot);snapshot=next;}snapshot_at=now;
  }
}
bool snow_top(u64 now){
  if(!active)return false;
  if(error[0]||!textures){fill_rect(0,0,400,240,sk_rgb(0xdfeaf2));text_draw(TR("Snow Storm"),200,100,16,sk_rgb(0x234a65),1);return true;}
  number_call("draw",now);return true;
}
static int num(json_t *j,const char *key){return (int)json_integer_value(json_object_get(j,key));}
static const char *str(json_t *j,const char *key){const char *s=json_string_value(json_object_get(j,key));return s?s:"";}
static bool is_mode(const char *s){return !strcmp(str(snapshot,"mode"),s);}
static void leave(void *unused){(void)unused;ui_close();}
static void chat_done(const char *text,void *ctx){
  (void)ctx;if(!context||!active)return;
  JSValue a=JS_NewString(context,text),r=call("chat",1,&a);JS_FreeValue(context,r);
}
// The touch keyboard keeps the match running (the system keyboard would pause it).
static void chat(void){osk_open(TR("Snow Storm chat (START: send)"),"",100,chat_done,NULL);}
void snow_input(u32 down,u32 held,u64 now){
  (void)held;
  if(down&KEY_B){if(context&&!error[0]&&is_mode("game"))ui_confirm(TR("Leave Snow Storm? A computer player will take your place."),TR("Leave"),leave,NULL);else ui_close();return;}
  if(!context||error[0]||!is_mode("game"))return;
  if(down&KEY_SELECT){static bool off;off=!off;snow_audio_mute(off);}
  if(down&KEY_X)invoke("make");
  if(down&KEY_A)invoke("throw");
  if(down&KEY_Y)invoke("trajectory");
  if(down&(KEY_L|KEY_R))invoke("zoom");
  if(down&(KEY_DLEFT|KEY_DRIGHT))invoke("aim");
  circlePosition pad;hidCircleRead(&pad);
  if(now-move_at>180&&(abs(pad.dx)>40||abs(pad.dy)>40)){
    int dx=(pad.dx-pad.dy)>40?2:(pad.dx-pad.dy)<-40?-2:0;
    int dy=(-pad.dx-pad.dy)>40?2:(-pad.dx-pad.dy)<-40?-2:0;
    JSValue args[]={JS_NewInt32(context,dx),JS_NewInt32(context,dy)},r=call("move",2,args);JS_FreeValue(context,r);move_at=now;
  }
}
void snow_draw(u64 now){
  if(sk_window(WIN_X,WIN_Y,WIN_W,WIN_H,TR("Snow Storm"))){if(context&&!error[0]&&is_mode("game"))ui_confirm(TR("Leave Snow Storm? A computer player will take your place."),TR("Leave"),leave,NULL);else ui_close();return;}
  sk_content(IN_X,IN_Y,IN_W,IN_H);
  if(error[0]){sk_paragraph(error,18,55,284,12,SK_TEXT,6);return;}
  if(!snapshot){sk_text(TR("Connecting to the Snow Storm lobby..."),18,62,11,SK_TEXT,0,284);return;}
  if(create_page){
    int *values[]={&arena,&teams,&players,&minutes,&level,&fill};
    const char *labels[]={"Arena","Teams (1 = solo)","Players per team","Minutes","Bot level","Fill after (min)"};
    for(int i=0;i<6;i++){
      char line[64];snprintf(line,sizeof line,"%s: %d",TR(labels[i]),*values[i]);sk_text(line,18,43+i*21,11,SK_TEXT,0,214);
      int change=sk_button(242,40+i*21,28,"-",true)?-1:sk_button(276,40+i*21,28,"+",true)?1:0;
      if(change){
        if(i==0){static const int a[]={1,2,3,7};int n=0;while(n<3&&a[n]!=arena)n++;arena=a[(n+change+4)%4];}
        else if(i==3){static const int a[]={2,3,5};int n=0;while(n<2&&a[n]!=minutes)n++;minutes=a[(n+change+3)%3];}
        else {int lo=i==4||i==5?0:1,hi=i==1?4:i==2?(teams==1?8:4):i==4?2:10;*values[i]=lo+(*values[i]-lo+change+hi-lo+1)%(hi-lo+1);}
        if(teams==1&&players<2)players=2;
        if(teams>1&&players>4)players=4;
      }
    }
    if(sk_button(18,174,90,TR("Back"),true))create_page=false;
    if(sk_button(120,174,182,TR("Create game"),net_online())){
      JSValue args[]={JS_NewInt32(context,arena),JS_NewInt32(context,teams),JS_NewInt32(context,players),JS_NewInt32(context,minutes),JS_NewInt32(context,level),JS_NewInt32(context,fill)};
      JSValue r=call("create",6,args);JS_FreeValue(context,r);create_page=false;
    }return;
  }
  // The server no longer has our match (bridge "ended"): say so once.
  static bool ended_shown;
  bool ended=json_is_true(json_object_get(snapshot,"ended"));
  if(ended&&!ended_shown)ui_toast(TR("That Snow Storm match has ended. Join or create another one."),now);
  ended_shown=ended;
  if(is_mode("list")){
    json_t *list=json_object_get(snapshot,"games");int n=json_array_size(list);if(page*4>=n)page=0;
    for(int i=0;i<4;i++){
      json_t *g=json_array_get(list,page*4+i);if(!g)break;char line[100];
      snprintf(line,sizeof line,"%s | %u/%d",str(g,"name"),(unsigned)json_array_size(json_object_get(g,"roster")),num(g,"capacity"));
      sk_text(line,16,45+i*25,11,SK_TEXT,0,210);
      if(sk_button(235,40+i*25,68,json_is_true(json_object_get(g,"running"))?TR("Playing"):TR("Join"),!json_is_true(json_object_get(g,"running"))))number_call("join",num(g,"id"));
    }
    if(!n)sk_paragraph(TR("No Snow Storm games in this room. Create one, or practise against computer players."),18,52,284,12,SK_TEXT,4);
    if(sk_button(18,145,86,TR("Refresh"),true))invoke("list");
    if(sk_button(115,145,86,TR("More"),n>4))page=(page+1)%((n+3)/4);
    if(sk_button(212,145,90,TR("Create"),net_online()))create_page=true;
    if(sk_button(18,174,284,TR("Practice (no credits)"),true)){
      Ent *e=world_ent(W.me);JSValue args[]={JS_NewInt64(context,W.me),JS_NewFloat64(context,e?e->look:0),JS_NewString(context,e?e->name:"You"),JS_NewFloat64(context,now)};
      JSValue r=call("practice",4,args);JS_FreeValue(context,r);
    }return;
  }
  if(is_mode("waiting")){
    json_t *g=json_object_get(snapshot,"waiting"),*list=json_object_get(g,"roster");char line[128];
    snprintf(line,sizeof line,TR("Arena %d | %d teams | %d min"),num(g,"arena"),num(g,"teams"),num(g,"minutes"));sk_text(line,18,44,11,SK_TEXT,0,284);
    for(size_t i=0;i<json_array_size(list)&&i<5;i++){json_t *p=json_array_get(list,i);snprintf(line,sizeof line,TR("Team %d: %s"),num(p,"team")+1,str(p,"name"));sk_text(line,18,64+i*16,11,SK_TEXT,0,284);}
    snprintf(line,sizeof line,num(g,"fill")?TR("Computer players fill places in %d sec."):TR("Waiting for players. The host can start now."),num(g,"fillIn"));sk_text(line,18,149,10,SK_TEXT,0,284);
    if(sk_button(18,174,86,TR("Chat"),true))chat();
    if(sk_button(116,174,186,TR("Start with bots"),(u32)num(g,"host")==W.me))number_call("start",num(g,"id"));
    return;
  }
  json_t *s=json_object_get(snapshot,"state"),*avatars=json_object_get(s,"avatars");
  if(is_mode("results")){
    sk_text(TR("Match finished"),160,43,14,SK_TEXT,1,280);
    for(size_t i=0;i<json_array_size(avatars)&&i<6;i++){json_t *p=json_array_get(avatars,i);char line[100];snprintf(line,sizeof line,TR("%s: %d points"),str(p,"name"),num(p,"score"));sk_text(line,18,64+i*16,11,SK_TEXT,0,284);}
    if(sk_button(18,174,284,TR("Back to Snow Storm lobby"),true))invoke("leave");
    return;
  }
  char line[140];int left=num(s,"left");snprintf(line,sizeof line,TR("%d:%02d   Health %d   Snowballs %d"),left/60,left%60,num(s,"hp"),num(s,"balls"));
  sk_text(line,160,41,11,SK_TEXT,1,284);
  // Rivals as touch buttons: tap one to target it (tap again for auto-aim at the nearest). D-Pad cycles.
  sk_text(json_is_true(json_object_get(snapshot,"manual"))?TR("Your target (tap it again for auto-aim)"):TR("Target: nearest rival (tap a name to choose)"),18,54,9,SK_DIM,0,284);
  int shown=0;
  for(size_t i=0;i<json_array_size(avatars)&&shown<8;i++){
    json_t *p=json_array_get(avatars,i);
    if(!json_is_true(json_object_get(p,"enemy")))continue;
    float bx=18+(shown%2)*144,by=68+(shown/2)*20;shown++;
    bool on=json_is_true(json_object_get(p,"target"));
    snprintf(line,sizeof line,TR("%s%s  %d hp"),on?"> ":"",str(p,"name"),num(p,"hp"));
    fill_round(bx,by,140,18,4,sk_rgb(on?0x9c6b00:0x7d7d7d));
    fill_round(bx+1,by+1,138,16,3,sk_rgb(on?0xffd34d:0xf4f4f0));
    sk_text(line,bx+70,by+3,10,SK_TEXT,1,132);
    if(sk_hit(bx,by,140,18))number_call("pick",(double)i);
  }
  if(!shown)sk_text(TR("No rivals left."),18,72,10,SK_DIM,0,284);
  if(sk_button_big(18,150,94,TR("Snowball (X)"),true))invoke("make");
  if(sk_button_big(116,150,90,TR("Throw (A)"),true))invoke("throw");
  static const char *trajectories[]={"Quick (Y)","Short arc (Y)","Long arc (Y)"};
  if(sk_button_big(210,150,92,TR(trajectories[num(snapshot,"trajectory")%3]),true))invoke("trajectory");
  if(sk_button(18,178,60,TR("Chat"),true))chat();
  if(sk_button(82,178,74,TR("Zoom (L/R)"),true))invoke("zoom");
  const char *hint=json_is_true(json_object_get(snapshot,"syncing"))?"Waiting for server turns...":num(snapshot,"queued")>60?"Catching up...":!strcmp(str(s,"status"),"countdown")?"Get ready!":"Pad: move · B: leave";
  sk_text(TR(hint),302,181,9,SK_DIM,2,140);
}
