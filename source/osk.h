#pragma once
#include "common.h"

typedef void (*OskDone)(const char *text, void *ctx);
/** Opens the touch-screen keyboard; done() receives the text when it is sent (not on cancel). */
void osk_open(const char *title, const char *initial, int max_chars, OskDone done, void *ctx);
/** Same keyboard for passwords: the field shows asterisks. */
void osk_open_secret(const char *title, int max_chars, OskDone done, void *ctx);
bool osk_active(void);
void osk_cancel(void);
/** Buttons while the keyboard is open (returns true when it consumed them). */
bool osk_input(u32 down, u32 held, u64 now);
/** Draws the keyboard over the whole bottom screen (returns true when open). */
bool osk_draw(u64 now);
