#include "gfx_color.h"
#include <assert.h>
#include <stdio.h>
int main(void){
  assert(gfx_color_multiply(0x80402010,0xffffffff)==0x80402010);
  assert(gfx_color_multiply(0x80402010,0)==0);
  assert(gfx_color_multiply(0xffffffff,0xff7f7f7f)==0xff7f7f7f);
  assert(gfx_color_multiply(0x80402010,0x80808080)==0x40201008);
  assert(gfx_color_multiply(0x11223344,0xff00ffff)==0x11003344);
  puts("PASS: ABGR modulation preserves default colors and multiplies all RGB/alpha channels without changing source pixels.");
}
