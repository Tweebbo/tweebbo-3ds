#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static JSValue noop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;(void)argc;(void)argv;return JS_NewInt32(ctx,0);}
static JSValue print(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;for(int i=0;i<argc;i++){const char *s=JS_ToCString(ctx,argv[i]);if(s){puts(s);JS_FreeCString(ctx,s);}}return JS_UNDEFINED;
}
static int eval(JSContext *ctx,const char *path){
  FILE *f=fopen(path,"rb");if(!f)return 1;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *b=malloc(n+1);if(!b){fclose(f);return 1;}size_t len=fread(b,1,n,f);fclose(f);b[len]=0;
  JSValue v=JS_Eval(ctx,b,len,path,JS_EVAL_TYPE_GLOBAL);free(b);int failed=JS_IsException(v);JS_FreeValue(ctx,v);
  if(failed){JSValue e=JS_GetException(ctx);const char *s=JS_ToCString(ctx,e);fprintf(stderr,"%s\n",s?s:"exception");JS_FreeCString(ctx,s);JSValue st=JS_GetPropertyStr(ctx,e,"stack");s=JS_ToCString(ctx,st);fprintf(stderr,"%s\n",s?s:"");JS_FreeCString(ctx,s);JS_FreeValue(ctx,st);JS_FreeValue(ctx,e);}return failed;
}
int main(int argc,char **argv){
  JSRuntime *rt=JS_NewRuntime();JS_SetMemoryLimit(rt,12*1024*1024);JS_SetMaxStackSize(rt,128*1024);JSContext *ctx=JS_NewContext(rt);JSValue g=JS_GetGlobalObject(ctx);
  const char *names[]={"nativeSprite","nativeRect","nativeText","nativeMeasure","nativeSend","nativeSound","nativeStopWalk"};for(unsigned i=0;i<7;i++)JS_SetPropertyStr(ctx,g,names[i],JS_NewCFunction(ctx,noop,names[i],0));
  JS_SetPropertyStr(ctx,g,"print",JS_NewCFunction(ctx,print,"print",1));JS_FreeValue(ctx,g);
  int failed=0;for(int i=1;i<argc&&!failed;i++)failed=eval(ctx,argv[i]);
  JSMemoryUsage memory;JS_ComputeMemoryUsage(rt,&memory);fprintf(stderr,"QuickJS retained heap: %lld bytes\n",(long long)memory.memory_used_size);
  JS_FreeContext(ctx);JS_FreeRuntime(rt);return failed;
}
