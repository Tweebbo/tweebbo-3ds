// Basic types shared by the console build and the PC tests.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __3DS__
#include <3ds/types.h>
#else
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
#endif

/** Monotonic milliseconds. */
u64 now_ms(void);
/** Random integer in [lo, hi). */
int rnd_range(int lo, int hi);

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/** Diagnostic log in sdmc:/3ds/Tweebbo/log.txt (cleared on start). */
void dbg_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
