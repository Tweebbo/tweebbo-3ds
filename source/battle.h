#pragma once
#include "common.h"
#include <jansson.h>
void battle_open(u32 player);
void battle_close(void);
bool battle_active(void);
bool battle_playing(void);
const char *battle_error(void);
json_t *battle_snapshot(void);
void battle_receive(const char *json,u64 now);
void battle_update(u64 now);
void battle_draw(int width,int height,u64 now);
void battle_move(int dx,int dy);
void battle_tap(int x,int y);
void battle_zoom(int delta);
void battle_centre(void);
void battle_preview(int arena);
void battle_command(const char *json);
void battle_status(char *out,int size);
