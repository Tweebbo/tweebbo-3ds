#pragma once
#include "battle.h"
enum { BATTLE_ARENA=16000,BATTLE_TEAMS,BATTLE_MINUTES,BATTLE_CREATE,BATTLE_PRACTICE,BATTLE_JOIN,BATTLE_TEAM,BATTLE_START,BATTLE_FILL,BATTLE_POWER,BATTLE_ZOOM,BATTLE_CENTRE,BATTLE_LEAVE,BATTLE_PAGE,BATTLE_SOUND };
typedef void (*BattleButton)(float,float,float,float,const char *,int,int);
typedef void (*BattleLabel)(const char *,float,float,float);
void battle_panel(float x,float y,float width,BattleButton button,BattleLabel label);
void battle_panel_action(int command,int argument);
void battle_panel_reset(void);
void battle_score(char *out,int size);
const char *battle_power_name(void);
