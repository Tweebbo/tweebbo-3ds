#pragma once
#include "common.h"
void snow_open(void);
void snow_close(bool notify);
void snow_receive(const u8 *data, int len);
void snow_frame(u64 now);
bool snow_top(u64 now);
void snow_draw(u64 now);
void snow_input(u32 down, u32 held, u64 now);
