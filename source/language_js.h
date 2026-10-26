#pragma once
#include "language.h"
#include "quickjs.h"

static JSValue tw_js_translate(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;
  if(!argc)return JS_UNDEFINED;
  const char *source=JS_ToCString(ctx,argv[0]);
  if(!source)return JS_EXCEPTION;
  JSValue translated=JS_NewString(ctx,TR(source));
  JS_FreeCString(ctx,source);
  return translated;
}
