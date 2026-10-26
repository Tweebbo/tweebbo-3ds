#include "fastfood.h"
#include "game_audio.h"
#include "gfx.h"
#include "avatar.h"
#include "net.h"
#include "quickjs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

static JSRuntime *runtime;
static JSContext *context;
static JSValue api;
static json_t *snapshot;
static bool active,textures;
static u64 deadline,snapshot_at;
static float ox,oy;
static char error[160],name[64];
static u64 look;
typedef union { size_t size; max_align_t align; } Allocation;
static void *qmalloc(JSMallocState *s,size_t n) {
  if(!n||n>SIZE_MAX-sizeof(Allocation)||s->malloc_size+n+sizeof(Allocation)>s->malloc_limit){dbg_log("Fast Food allocation refused: %lu bytes; used %lu",(unsigned long)n,(unsigned long)s->malloc_size);return NULL;}
  Allocation *p=malloc(n+sizeof *p);if(!p){dbg_log("Fast Food heap allocation failed: %lu",(unsigned long)n);return NULL;}p->size=n;
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
  if(n>SIZE_MAX-sizeof *p||s->malloc_size-old+n>s->malloc_limit){dbg_log("Fast Food reallocation refused: %lu bytes; old %lu; used %lu",(unsigned long)n,(unsigned long)old,(unsigned long)s->malloc_size);return NULL;}
  p=realloc(p,n+sizeof *p);if(!p){dbg_log("Fast Food heap reallocation failed: %lu",(unsigned long)n);return NULL;}p->size=n;s->malloc_size=s->malloc_size-old+n;return p+1;
}
static size_t qusable(const void *ptr){return ptr?((const Allocation*)ptr-1)->size:0;}

static int interrupted(JSRuntime *rt,void *opaque){(void)rt;(void)opaque;return now_ms()>deadline;}
static double arg(JSContext *ctx,JSValueConst v){double n=0;JS_ToFloat64(ctx,&n,v);return isfinite(n)?n:0;}
static u32 rgba(double value,double alpha){unsigned c=(unsigned)value;return ((u32)fmax(0,fmin(255,alpha*255))<<24)|((c&255)<<16)|(c&0xff00)|(c>>16);}
static JSValue sprite(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<11)return JS_UNDEFINED;double a[11];for(int i=0;i<11;i++)a[i]=arg(ctx,argv[i]);
  gfx_fastfood_sprite(a[0],a[1]+ox,a[2]+oy,a[3],a[4],rgba(a[5],a[6]),a[7]+ox,a[8]+oy,a[9]+ox,a[10]+oy);return JS_UNDEFINED;
}
static JSValue head(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc>=4)avatar_draw_head((u64)arg(ctx,argv[0]),2,arg(ctx,argv[1])+ox,arg(ctx,argv[2])+oy,arg(ctx,argv[3]));return JS_UNDEFINED;
}
static JSValue rectangle(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<7)return JS_UNDEFINED;double a[7];for(int i=0;i<7;i++)a[i]=arg(ctx,argv[i]);
  fill_round(a[0]+ox,a[1]+oy,a[2],a[3],a[4],rgba(a[5],a[6]));return JS_UNDEFINED;
}
static JSValue text(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<6)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  text_draw(s,arg(ctx,argv[1])+ox,arg(ctx,argv[2])+oy,arg(ctx,argv[3]),rgba(arg(ctx,argv[4]),1),arg(ctx,argv[5]));JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static JSValue measure(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<2)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  double width=text_width(s,arg(ctx,argv[1]));JS_FreeCString(ctx,s);return JS_NewFloat64(ctx,width);
}
static JSValue send_text(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<1)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  bool ok=net_fastfood_send(s);JS_FreeCString(ctx,s);return JS_NewBool(ctx,ok);
}
static JSValue disconnect_game(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)ctx;(void)self;(void)argc;(void)argv;net_fastfood_enable(false);return JS_UNDEFINED;
}
static JSValue sound(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<3)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;game_audio_play(s,arg(ctx,argv[1]),JS_ToBool(ctx,argv[2]));JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static void exception(void){JSValue e=JS_GetException(context);const char *s=JS_ToCString(context,e);dbg_log("Fast Food: %s",s?s:"runtime error");snprintf(error,sizeof error,"Fast Food stopped safely. Close and reopen it.");JS_FreeCString(context,s);JS_FreeValue(context,e);}
static JSValue call(const char *name,int n,JSValue *args){
  deadline=now_ms()+250;JSValue fn=JS_GetPropertyStr(context,api,name),result=JS_Call(context,fn,api,n,args);
  JS_FreeValue(context,fn);for(int i=0;i<n;i++)JS_FreeValue(context,args[i]);if(JS_IsException(result))exception();return result;
}
static void invoke(const char *name){if(!context||error[0])return;JSValue result=call(name,0,NULL);JS_FreeValue(context,result);}
static bool init(void){
  api=JS_UNDEFINED;
  static const JSMallocFunctions allocator={qmalloc,qfree,qrealloc,qusable};
  runtime=JS_NewRuntime2(&allocator,NULL);if(!runtime)return false;
  JS_SetMemoryLimit(runtime,4u*1024*1024);JS_SetMaxStackSize(runtime,128u*1024);JS_SetInterruptHandler(runtime,interrupted,NULL);
  deadline=now_ms()+5000;context=JS_NewContext(runtime);if(!context)return false;
  JSValue global=JS_GetGlobalObject(context);
  struct {const char *name;JSCFunction *fn;int argc;} fns[]={{"nativeSprite",sprite,11},{"nativeRect",rectangle,7},{"nativeText",text,6},{"nativeMeasure",measure,2},{"nativeSendText",send_text,1},{"nativeDisconnect",disconnect_game,0},{"nativeHead",head,8},{"nativeSound",sound,3}};
  for(unsigned i=0;i<sizeof fns/sizeof *fns;i++)JS_SetPropertyStr(context,global,fns[i].name,JS_NewCFunction(context,fns[i].fn,fns[i].name,fns[i].argc));
#ifdef __vita__
  FILE *file=fopen("app0:fastfood/game.js","rb");
#else
  FILE *file=fopen("romfs:/fastfood/game.js","rb");
#endif
  if(!file){JS_FreeValue(context,global);return false;}fseek(file,0,SEEK_END);long len=ftell(file);rewind(file);
  if(len<=0||len>1024*1024){fclose(file);JS_FreeValue(context,global);return false;}
  char *source=malloc(len+1);if(!source){fclose(file);JS_FreeValue(context,global);return false;}
  size_t count=fread(source,1,len,file);fclose(file);source[count]=0;
  JSValue result=JS_Eval(context,source,count,"fastfood/game.js",JS_EVAL_TYPE_GLOBAL);free(source);
  bool ok=!JS_IsException(result);if(!ok)exception();JS_FreeValue(context,result);
  api=JS_GetPropertyStr(context,global,"FastFood");JS_FreeValue(context,global);if(ok)invoke("open");return ok;
}
void fastfood_close(void){
  if(active)game_audio_free();
  active=false;net_fastfood_enable(false);
  if(context){JS_FreeValue(context,api);JS_FreeContext(context);context=NULL;}
  if(runtime){JS_FreeRuntime(runtime);runtime=NULL;}
  json_decref(snapshot);snapshot=NULL;
}
void fastfood_open(const char *player,u64 appearance){fastfood_close();active=true;error[0]=0;snapshot_at=0;snprintf(name,sizeof name,"%s",player);look=appearance;}
bool fastfood_active(void){return active;}
const char *fastfood_error(void){return error;}
json_t *fastfood_snapshot(void){return snapshot;}
void fastfood_update(u64 now){
  if(!active){if(textures){gfx_fastfood_free();textures=false;}return;}
  if(error[0])return;
  if(!context){if(!game_audio_load(1))dbg_log("Fast Food audio unavailable");if(!init()){if(!error[0])snprintf(error,sizeof error,"Fast Food could not start. Close and retry.");return;}}
  if(!textures){if(!gfx_fastfood_load()){snprintf(error,sizeof error,"Fast Food graphics could not load. Close and retry.");return;}textures=true;}
  for(int n=0;n<8;n++){char *message=net_fastfood_receive();if(!message)break;JSValue args[]={JS_NewString(context,message),JS_NewFloat64(context,now)},r=call("receive",2,args);JS_FreeValue(context,r);free(message);if(error[0])return;}
  JSValue at=JS_NewFloat64(context,now),result=call("tick",1,&at);JS_FreeValue(context,result);
  if(now-snapshot_at>=100){result=call("status",0,NULL);const char *s=JS_ToCString(context,result);json_t *next=s?json_loads(s,0,NULL):NULL;JS_FreeCString(context,s);JS_FreeValue(context,result);if(next){json_decref(snapshot);snapshot=next;}snapshot_at=now;}
}
void fastfood_practice(int goal){if(!context||error[0])return;net_fastfood_enable(false);JSValue args[]={JS_NewString(context,name),JS_NewFloat64(context,look),JS_NewInt32(context,goal),JS_NewFloat64(context,now_ms())},r=call("practice",4,args);JS_FreeValue(context,r);}
void fastfood_online(void){if(context&&!error[0]){net_fastfood_enable(false);invoke("connect");net_fastfood_enable(true);}}
void fastfood_action(const char *action){if(!context||error[0])return;JSValue a=JS_NewString(context,action),r=call("action",1,&a);JS_FreeValue(context,r);}
void fastfood_command(const char *json){if(!context||error[0])return;JSValue a=JS_NewString(context,json),r=call("command",1,&a);JS_FreeValue(context,r);}
void fastfood_menu(void){invoke("leave");}
void fastfood_draw(float x,float y,int width,int height){
  if(!active||!context||!textures||error[0])return;
  ox=x;oy=y;
  JSValue args[]={JS_NewInt32(context,width),JS_NewInt32(context,height)},r=call("resize",2,args);JS_FreeValue(context,r);invoke("draw");
}
