#pragma once
#include "common.h"

enum { F_NOTE=1,F_TROPHY,F_MOODLIGHT,F_GIFT,F_MANNEQUIN,F_CHANGING,F_WIRED,F_FREEZE,F_BANZAI,F_GIFT_BUY,F_CLOCK,F_PUSHBOX,F_FOOTBALL_GATE,F_MAGIC_TILE,F_WALK_MAGIC_TILE,F_WIRED_CHEST,F_WIRED_CONTRACT,F_WIRED_LINK,F_WIRED_LABEL };
typedef struct {const char *key,*label;char *value;int limit;u16 capacity;bool editable;} FurnitureField;
enum { FURNITURE_FIELDS=104 };
typedef struct {
  int kind,field_count,selected,target;u32 item,revision;u16 definition;
  bool pending,editable,owned,can_apply,variable_definition,compact_motion;char name[128],status[192];
  FurnitureField fields[FURNITURE_FIELDS];
  char values[8192];u16 values_used;
} FurniturePanel;
_Static_assert(sizeof(FurniturePanel)<=12288,"Furniture editor memory budget");
extern FurniturePanel furniture_panel;
void furniture_receive(const char *text);
void furniture_reset(void);
void furniture_open(u32 item);
void furniture_gift(u16 offer);
bool furniture_edit(const char *text);
void furniture_save(void);
void furniture_apply(void);
void furniture_choice(int field,int step);
bool furniture_has_choices(int field);
int furniture_display_count(void);
int furniture_display_index(int visible);
const char *furniture_choice_label(int field);
void furniture_target(int step,bool toggle);
const char *furniture_target_label(void);
/** -1 when there is no target, else whether the target of furniture_target_label() is selected (drawn as a checkbox). */
int furniture_target_check(void);
const char *furniture_target_kind(void);
void furniture_toggle_target_kind(void);
bool furniture_player_motion(void);
bool furniture_target_switchable(void);
const char *furniture_help_text(void);
bool furniture_target_picker(void);
u32 furniture_light_color(void);
u8 furniture_light_alpha(void);
bool furniture_light_background(void);
const char *furniture_arena_text(void);
void furniture_throw(void);
bool furniture_can_throw(void);

u32 furniture_item_color(u32 item);
bool furniture_item_look(u32 item,u64 *look);
const char *furniture_operation_label(void);
const char *furniture_clock_text(u32 item);
