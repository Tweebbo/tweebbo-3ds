#pragma once
#include <stdint.h>

static inline uint32_t gfx_color_multiply(uint32_t color,uint32_t modulation){
  uint32_t result=0;
  for(unsigned shift=0;shift<32;shift+=8){
    uint32_t channel=((color>>shift)&255)*((modulation>>shift)&255);
    result|=((channel+127)/255)<<shift;
  }
  return result;
}
