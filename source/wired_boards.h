#pragma once
#include "wired_services.h"
extern WiredServicePanel wired_board_panel;
enum { WB_BACK=1,WB_REFRESH,WB_PREVIOUS_VARIABLE,WB_NEXT_VARIABLE,WB_PREVIOUS_CHUNK,WB_NEXT_CHUNK };
void wired_boards_open(void);
void wired_boards_close(void);
void wired_boards_reset(void);
void wired_boards_update(void);
bool wired_boards_receive(const char *text);
void wired_boards_select(int index);
void wired_boards_action(int action);
void wired_boards_row(int index,char *title,size_t title_size,char *detail,size_t detail_size);
void wired_boards_draw3ds(u64 now);
