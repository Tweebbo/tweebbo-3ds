#pragma once
#include <stdint.h>

typedef struct {float row[3][4];} GfxHighlightMatrix;
/* FurnitureHighlightFilterFactory.as; offsets are normalized to a 0..1 sampler. */
static inline void gfx_highlight_matrix(int kind,uint32_t rgb,int strength,GfxHighlightMatrix *matrix){
    *matrix=(GfxHighlightMatrix){{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
    float s=(strength<0?0:strength>100?100:strength)/100.f;
    if(kind==2){for(int row=0;row<3;row++)matrix->row[row][row]=1-.65f*s;}
    else if(kind==4){for(int row=0;row<3;row++){matrix->row[row][row]=1+.78f*s;matrix->row[row][3]=36*s/255.f;}}
    else if(kind==3){
        float color[3]={(rgb>>16&255)/255.f,(rgb>>8&255)/255.f,(rgb&255)/255.f};
        float amount=s*((rgb&0xffffff)?1.35f:1),a=.65f*amount,cross=.05f*a;
        for(int row=0;row<3;row++){for(int column=0;column<3;column++)matrix->row[row][column]=cross*color[column];matrix->row[row][row]=1-a+a*color[row];matrix->row[row][3]=color[row]*35*amount/255.f;}
    }
}
