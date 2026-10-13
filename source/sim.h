// Simulated server for offline play. Reproduces the rules of
// the Tweebbo server room logic (500 ms tick, A*, seats, gestures,
// drinks) and speaks the same binary protocol as the real server: the
// client cannot tell this simulation from an online connection.
#pragma once
#include "common.h"

void sim_start(const char *room_key, u64 now);
/** Message from the local player (client → server). */
void sim_receive(const u8 *data, int len);
void sim_update(u64 now);

// Hooks implemented by the application.
/** Message from the simulated server to the local player. */
void sim_deliver(const u8 *data, int len);
/** Local profile (replaces the server database). */
void profile_load(char *name, int cap, u64 *look, u32 *credits);
void profile_save(const char *name, u64 look, u32 credits);
