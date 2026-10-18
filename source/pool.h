// Lido activities (The Pool, Diving Deck, Rooftop Rumble): changing booths and swimsuits, the
// diving tower and Wobble Squabble, driven by the server's ACTIVITY state like the web edition
// (client/src/activities.ts, activity-scene.ts). The server announces it to clients that send
// FEATURE_ACTIVITIES in HELLO.
#pragma once
#include "common.h"
#include "world.h"

/** s2c ACTIVITY: {"now", "state"} (server room/activities.rs). */
void pool_receive(const char *json, u64 now);
/** A new room: forget the previous activity state. */
void pool_room_changed(void);
/** Is this a Lido room (pool_a, pool_b, md_a)? */
bool pool_room(void);
/** Swimsuit colour of an entity (0-15), or -1 in clothes. */
int pool_swimsuit(u32 eid);
/** Look with the swimsuit colour and the AV_BATHING bit when the entity wears one. */
u64 pool_look(const Ent *e, u64 look);
/** Entities drawn by the activity itself (on the pole, on the tower or diving). */
bool pool_hides(const Ent *e);
/** Closed state of scene switch k (0 and 1: booth curtains, 2: the lift door). */
bool pool_switch_closed(int k);
/** Camera target in world coordinates while the activity needs it; false otherwise. */
bool pool_camera(float *wx, float *wy, u64 now);
/** The jumper's own top screen shows the tower instead of the room. */
bool pool_tower_view(void);
void pool_draw_tower(u64 now);
/** Wobble players, the countdown, the diver in the room and the splashes (after the room). */
void pool_draw_room(u64 now);
/** "Press A" target of the Lido rooms (signposts, booths, the lift, the pole); false if none. */
bool pool_context(char *label, int cap, u64 now);
void pool_context_run(u64 now);
/** The bottom-screen controls are up (booth, diving, voting, Wobble Squabble). */
bool pool_panel_active(void);
/** Buttons while the controls are up; true when they were used. */
bool pool_input(u32 down, u32 held, u64 now);
void pool_draw_panel(u64 now);
/** A signpost walk finished: continue into the other Lido area. */
void pool_poll(u64 now);
