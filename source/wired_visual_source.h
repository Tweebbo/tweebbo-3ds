#pragma once
#include <math.h>
#include <stdbool.h>

static inline float wired_visual_ease(float value,unsigned mode){
  float t=fmaxf(0,fminf(1,value));
  return mode==2?t*t:mode==3?1-(1-t)*(1-t):mode==4?(t<.5f?2*t*t:1-powf(-2*t+2,2)/2):t;
}
static inline unsigned wired_visual_quantize(float value){
  return !isfinite(value)||value<=0?0:value>=100?100:(unsigned)fmaxf(5,floorf(value/5+.5f)*5);
}
static inline float wired_visual_strength(unsigned target,unsigned from,unsigned elapsed,bool animation,unsigned easing){
  return animation?floorf(target*(1-cosf((elapsed%1000)*.00628318530718f))/2+.5f):easing?from+((float)target-from)*wired_visual_ease(elapsed/500.f,easing):target;
}
