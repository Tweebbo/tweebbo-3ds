// Console payments: the console asks the hotel for a short-lived pay link and shows it as a
// QR code; a phone or PC opens it and pays (chips or Tweebbo Club) for this character. The
// console never handles money: it only shows the code and waits for the server to say "paid".
#pragma once
#include "common.h"

typedef enum { PAY_IDLE, PAY_LOADING, PAY_READY, PAY_PAID, PAY_EXPIRED, PAY_ERROR } PayState;

/** Asks for a new link: kind is "chips" or "club". */
void paylink_open(const char *kind, u64 now);
void paylink_close(void);
/** Call every frame: collects answers and checks every few seconds whether it was paid. */
void paylink_poll(u64 now);
PayState paylink_state(void);
/** The error or result text for PAY_ERROR / PAY_PAID. */
const char *paylink_message(void);
/** Seconds left before the code stops opening new payments. */
int paylink_seconds_left(u64 now);
/** QR modules per side (0 when there is no code yet) and whether a module is dark. */
int paylink_qr_size(void);
bool paylink_qr_dark(int x, int y);
