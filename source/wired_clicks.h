#ifndef WIRED_CLICKS_H
#define WIRED_CLICKS_H
#include "proto.h"
#include <stdbool.h>
void wired_clicks_reset(void);
bool wired_clicks_receive(const char *text);
int wired_clicks_user_mode(void);
bool wired_clicks_furniture_through(void);
bool wired_clicks_active(void);
bool wired_clicks_ignored(void);
void wired_clicks_toggle(void);
void wired_clicks_user(u32 id);
void wired_clicks_furniture(u32 id);
u32 wired_clicks_menu_user(void);
#endif
