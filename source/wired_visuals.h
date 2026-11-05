#pragma once
#include "common.h"

bool wired_visuals_receive(const char *json);
void wired_visuals_reset(void);
void wired_visuals_forget(bool user,u32 id);
u8 wired_visuals_alpha(bool user,u32 id,u64 now);
u32 wired_visuals_tint(bool user,u32 id,u64 now);
bool wired_visuals_click_through(bool user,u32 id);
bool wired_visuals_highlight(bool user,u32 id,u64 now,int *kind,u32 *rgb,int *strength);
void wired_visuals_draw(bool user,u32 id,float x,float y,float scale,bool hover,u64 now);
/** Draws the room labels queued by wired_visuals_draw this frame, inside the given viewport. */
void wired_visuals_draw_labels(float left,float top,float right,float bottom);
size_t wired_visuals_bytes(void);
