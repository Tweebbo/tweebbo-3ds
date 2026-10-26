#include "battle.h"
#include "game_audio.h"
#include "gfx.h"
#include "avatar.h"
#include "net.h"
#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

static JSRuntime *runtime;
static JSContext *context;
static JSValue api;
static json_t *snapshot;
static char *pending;
static u64 received,deadline,server_now;
static u32 player;
static int textures,selected=5;
static bool active;
static char error[160];
typedef union {size_t size;max_align_t align;} Allocation;
static void *qmalloc(JSMallocState *s,size_t n){
  if(!n||n>SIZE_MAX-sizeof(Allocation)||n+sizeof(Allocation)>s->malloc_limit-s->malloc_size)return NULL;
  Allocation *p=malloc(n+sizeof *p);if(!p)return NULL;p->size=n;s->malloc_count++;s->malloc_size+=n+sizeof *p;return p+1;
}
static void qfree(JSMallocState *s,void *ptr){if(!ptr)return;Allocation *p=(Allocation*)ptr-1;s->malloc_count--;s->malloc_size-=p->size+sizeof *p;free(p);}
static void *qrealloc(JSMallocState *s,void *ptr,size_t n){
  if(!ptr)return qmalloc(s,n);
  if(!n){qfree(s,ptr);return NULL;}
  Allocation *p=(Allocation*)ptr-1;size_t old=p->size;
  if(n>SIZE_MAX-sizeof *p||n>s->malloc_limit-(s->malloc_size-old))return NULL;
  p=realloc(p,n+sizeof *p);if(!p)return NULL;p->size=n;s->malloc_size=s->malloc_size-old+n;return p+1;
}
static size_t qusable(const void *ptr){return ptr?((const Allocation*)ptr-1)->size:0;}
static int interrupted(JSRuntime *rt,void *opaque){(void)rt;(void)opaque;return now_ms()>deadline;}
static double arg(JSContext *ctx,JSValueConst v){double n=0;JS_ToFloat64(ctx,&n,v);return isfinite(n)?n:0;}
static JSValue sprite(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<11)return JS_UNDEFINED;double a[11];for(int i=0;i<11;i++)a[i]=arg(ctx,argv[i]);
  unsigned c=a[5];u32 color=((u32)fmax(0,fmin(255,a[6]*255))<<24)|((c&255)<<16)|(c&0xff00)|(c>>16);
  gfx_battle_sprite(a[0],a[1],a[2],a[3],a[4],color,a[7],a[8],a[9],a[10]);return JS_UNDEFINED;
}
static JSValue figure(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc>=5)avatar_draw((u64)arg(ctx,argv[0]),arg(ctx,argv[1]),POSE_BOUNCE,0,0,arg(ctx,argv[1]),0,false,arg(ctx,argv[2]),arg(ctx,argv[3]),arg(ctx,argv[4]),255);return JS_UNDEFINED;
}
static JSValue blend(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;if(argc)gfx_battle_blend(arg(ctx,argv[0]));return JS_UNDEFINED;}
static JSValue send_command(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(!argc)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;send_activity(s);JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static JSValue sound(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<3)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;game_audio_play(s,arg(ctx,argv[1]),JS_ToBool(ctx,argv[2]));JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static void exception(void){
  JSValue e=JS_GetException(context);const char *s=JS_ToCString(context,e);dbg_log("Battle Ball: %s",s?s:"runtime error");
  snprintf(error,sizeof error,"Battle Ball stopped safely. Close and reopen it.");JS_FreeCString(context,s);JS_FreeValue(context,e);
}
static JSValue call(const char *name,int n,JSValue *args){
  deadline=now_ms()+250;JSValue fn=JS_GetPropertyStr(context,api,name),r=JS_Call(context,fn,api,n,args);JS_FreeValue(context,fn);
  for(int i=0;i<n;i++)JS_FreeValue(context,args[i]);
  if(JS_IsException(r))exception();
  return r;
}
static void numbers(const char *name,int n,double a,double b,double c){
  if(!context||error[0])return;
  double values[]={a,b,c};JSValue args[3];for(int i=0;i<n;i++)args[i]=JS_NewFloat64(context,values[i]);
  JSValue r=call(name,n,args);JS_FreeValue(context,r);
}
static bool init(void){
  static const JSMallocFunctions allocator={qmalloc,qfree,qrealloc,qusable};api=JS_UNDEFINED;
  runtime=JS_NewRuntime2(&allocator,NULL);if(!runtime)return false;
  JS_SetMemoryLimit(runtime,4u*1024*1024);JS_SetMaxStackSize(runtime,128u*1024);JS_SetInterruptHandler(runtime,interrupted,NULL);deadline=now_ms()+5000;
  context=JS_NewContext(runtime);if(!context)return false;JSValue global=JS_GetGlobalObject(context);
  struct {const char *name;JSCFunction *fn;int argc;} fns[]={{"nativeSprite",sprite,11},{"nativeAvatar",figure,5},{"nativeBlend",blend,1},{"nativeSendText",send_command,1},{"nativeSound",sound,3}};
  for(unsigned i=0;i<sizeof fns/sizeof *fns;i++)JS_SetPropertyStr(context,global,fns[i].name,JS_NewCFunction(context,fns[i].fn,fns[i].name,fns[i].argc));
#ifdef __vita__
  FILE *file=fopen("app0:battle/game.js","rb");
#else
  FILE *file=fopen("romfs:/battle/game.js","rb");
#endif
  if(!file){JS_FreeValue(context,global);return false;}fseek(file,0,SEEK_END);long len=ftell(file);rewind(file);
  char *source=len>0&&len<2*1024*1024?malloc(len+1):NULL;if(!source){fclose(file);JS_FreeValue(context,global);return false;}
  size_t count=fread(source,1,len,file);fclose(file);source[count]=0;
  JSValue result=JS_Eval(context,source,count,"battle/game.js",JS_EVAL_TYPE_GLOBAL);free(source);bool ok=!JS_IsException(result);if(!ok)exception();JS_FreeValue(context,result);
  api=JS_GetPropertyStr(context,global,"Battle");JS_FreeValue(context,global);if(ok)numbers("open",1,player,0,0);return ok&&JS_IsObject(api);
}
void battle_close(void){
  if(active){
    json_t *players=json_object_get(snapshot,"players");for(size_t i=0;i<json_array_size(players);i++)if((u32)json_integer_value(json_object_get(json_array_get(players,i),"eid"))==player){send_activity("{\"op\":\"leave\"}");break;}
    game_audio_free();
  }
  active=false;
  if(context){JS_FreeValue(context,api);JS_FreeContext(context);context=NULL;}if(runtime){JS_FreeRuntime(runtime);runtime=NULL;}
  json_decref(snapshot);snapshot=NULL;free(pending);pending=NULL;
}
void battle_open(u32 id){battle_close();active=true;player=id;selected=5;error[0]=0;send_activity("{\"op\":\"sync\"}");}
bool battle_active(void){return active;}
bool battle_playing(void){return active&&!error[0]&&json_is_object(snapshot)&&json_integer_value(json_object_get(snapshot,"phase"))==1;}
const char *battle_error(void){return error;}
json_t *battle_snapshot(void){return snapshot;}
void battle_receive(const char *json,u64 now){
  if(!active||strlen(json)>256*1024)return;
  json_t *packet=json_loads(json,0,NULL),*state=json_object_get(packet,"state"),*game=json_object_get(state,"battle");
  if(game){json_incref(game);json_decref(snapshot);snapshot=game;free(pending);pending=strdup(json);received=now;server_now=json_integer_value(json_object_get(packet,"now"));
    int arena=json_integer_value(json_object_get(game,"arena"));if(arena>=1&&arena<=5)selected=arena;}
  json_decref(packet);
}
void battle_update(u64 now){
  (void)now;if(!active){if(textures){gfx_battle_free();textures=0;}return;}if(error[0])return;
  if(!context){if(!game_audio_load(2))dbg_log("Battle Ball audio unavailable");if(!init()){if(!error[0])snprintf(error,sizeof error,"Battle Ball could not start. Close and retry.");return;}}
  if(pending){JSValue args[]={JS_NewString(context,pending),JS_NewFloat64(context,received)},r=call("receive",2,args);JS_FreeValue(context,r);free(pending);pending=NULL;}
  numbers("audio",1,now,0,0);
  if(textures!=selected){if(!gfx_battle_load(selected)){snprintf(error,sizeof error,"Battle Ball graphics could not load. Close and retry.");textures=0;return;}textures=selected;}
}
void battle_draw(int width,int height,u64 now){
  if(!active||!context||!textures||error[0])return;
  static const u32 backgrounds[]={0xff544919,0xffc9bb90,0xff836d27,0xff4b7147,0xff503c40,0xff000000};
  fill_rect(0,0,width,height,backgrounds[selected]);numbers("resize",2,width,height,0);numbers("draw",1,now,0,0);gfx_battle_blend(0);
}
void battle_move(int dx,int dy){numbers("move",2,dx,dy,0);}
void battle_tap(int x,int y){numbers("tap",2,x,y,0);}
void battle_zoom(int delta){numbers("zoom",1,delta,0,0);}
void battle_centre(void){numbers("centre",0,0,0,0);}
void battle_preview(int arena){if(arena<1||arena>5)return;selected=arena;numbers("preview",1,arena,0,0);}
void battle_command(const char *json){if(active)send_activity(json);}
void battle_status(char *out,int size){
  u64 now=server_now+now_ms()-received,starts=json_integer_value(json_object_get(snapshot,"starts")),ends=json_integer_value(json_object_get(snapshot,"ends"));
  if(now<starts)snprintf(out,size,"Ready: %llu",(unsigned long long)((starts-now+999)/1000));
  else {unsigned seconds=ends>now?(ends-now+999)/1000:0;snprintf(out,size,"%u:%02u",seconds/60,seconds%60);}
}
