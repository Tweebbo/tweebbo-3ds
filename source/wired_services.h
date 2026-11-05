#pragma once
#include "common.h"

enum { WS_BOTS=1,WS_GROUPS,WS_QUESTS };
enum { WS_LIST,WS_DETAIL,WS_FORM,WS_CONFIRM,WS_POSITION };
enum { WS_BACK=1,WS_REFRESH,WS_NEW,WS_PREV,WS_NEXT,WS_REVIEW,WS_CONFIRM_ACTION,WS_PLACE,WS_PICKUP,WS_DELETE,WS_RENAME,WS_OUTFIT,WS_VISIT,WS_JOIN,WS_LEAVE,WS_EQUIP,WS_LINK_ROOM,WS_QUEST_START,WS_QUEST_CANCEL,WS_NORTH,WS_EAST,WS_SOUTH,WS_WEST };
#define WS_BUTTONS 8
#define WS_PAGE_ROWS 4

typedef struct {int action;bool enabled;char label[64];} WiredServiceButton;
typedef struct {
    int kind,view,page,rows,selected,button_count,field_count,tile_x,tile_y;
    bool open,pending;
    u32 revision;
    char title[128],body[1200],status[160],name[201],description[1021];
    WiredServiceButton buttons[WS_BUTTONS];
} WiredServicePanel;
extern WiredServicePanel wired_service_panel;

void wired_services_open(int kind);
void wired_services_close(void);
void wired_services_reset(void);
void wired_services_update(void);
bool wired_services_receive(const char *text);
void wired_services_quest_progress(long long quest,long long progress);
void wired_services_select(int index);
void wired_services_action(int action);
bool wired_services_edit(int field,const char *text);
int wired_services_field_limit(int field);
const char *wired_services_field_label(int field);
const char *wired_services_field_value(int field);
void wired_services_row(int index,char *title,size_t title_size,char *detail,size_t detail_size);
u64 wired_services_preview_look(void);

void wired_services_draw3ds(u64 now);
