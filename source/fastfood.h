#pragma once
#include "common.h"
#include <jansson.h>
void fastfood_open(const char *name,u64 look);
void fastfood_close(void);
bool fastfood_active(void);
const char *fastfood_error(void);
json_t *fastfood_snapshot(void);
void fastfood_update(u64 now);
void fastfood_practice(int goal);
void fastfood_online(void);
void fastfood_action(const char *action);
void fastfood_command(const char *json);
void fastfood_menu(void);
void fastfood_draw(float x,float y,int width,int height);
