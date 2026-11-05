#pragma once
#include "gfx_highlight.h"

static inline void highlight_tev_stage(int stage,GPU_COMBINEFUNC operation,GPU_TEVOP_RGB component,u32 constant){
    C3D_TexEnv *env=C3D_GetTexEnv(stage);C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env,component,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env,C3D_RGB,operation);C3D_TexEnvColor(env,constant);
}
static inline void highlight_tev(int kind,u32 rgb,int strength,u32 base){
    GfxHighlightMatrix m;gfx_highlight_matrix(kind,rgb,strength,&m);
    float r=(base&255)/255.f,g=(base>>8&255)/255.f,b=(base>>16&255)/255.f;
    for(int stage=0;stage<6;stage++)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    if(kind==3){
        float qr=m.row[1][0],qg=m.row[0][1],qb=m.row[0][2];
        highlight_tev_stage(0,GPU_MODULATE,GPU_TEVOP_RGB_SRC_R,C2D_Color32f(qr*r,qr*r,qr*r,1));
        highlight_tev_stage(1,GPU_MULTIPLY_ADD,GPU_TEVOP_RGB_SRC_G,C2D_Color32f(qg*g,qg*g,qg*g,1));
        highlight_tev_stage(2,GPU_MULTIPLY_ADD,GPU_TEVOP_RGB_SRC_B,C2D_Color32f(qb*b,qb*b,qb*b,1));
        highlight_tev_stage(3,GPU_MULTIPLY_ADD,GPU_TEVOP_RGB_SRC_COLOR,C2D_Color32f((m.row[0][0]-qr)*r,(m.row[1][1]-qg)*g,(m.row[2][2]-qb)*b,1));
        C3D_TexEnv *env=C3D_GetTexEnv(4);C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_PREVIOUS);C3D_TexEnvFunc(env,C3D_RGB,GPU_ADD);C3D_TexEnvColor(env,C2D_Color32f(m.row[0][3],m.row[1][3],m.row[2][3],1));
    }else{
        float scale=kind==4?2:1;
        highlight_tev_stage(0,GPU_MODULATE,GPU_TEVOP_RGB_SRC_COLOR,C2D_Color32f(m.row[0][0]*r/scale,m.row[1][1]*g/scale,m.row[2][2]*b/scale,1));
        C3D_TexEnvScale(C3D_GetTexEnv(0),C3D_RGB,kind==4?GPU_TEVSCALE_2:GPU_TEVSCALE_1);
        C3D_TexEnv *env=C3D_GetTexEnv(1);C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_PREVIOUS);C3D_TexEnvFunc(env,C3D_RGB,GPU_ADD);C3D_TexEnvColor(env,C2D_Color32f(m.row[0][3],m.row[1][3],m.row[2][3],1));
    }
    C3D_TexEnv *last=C3D_GetTexEnv(5);C3D_TexEnvSrc(last,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PREVIOUS);C3D_TexEnvFunc(last,C3D_Alpha,GPU_MODULATE);
}
