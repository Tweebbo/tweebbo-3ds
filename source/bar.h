// After Hours Bar ("afterhours"): the bartender's menu, the actions of the held drink or smoke
// and their gestures, from the server's bar messages on the ACTIVITY channel (server
// room/bar.rs), like client/src/bar.ts of the web edition.
#pragma once
#include "common.h"
#include "world.h"

#define BAR_COUNTER 1729
#define BAR_EIGHT_BALL 62014
#define BAR_DICE 62015

/** ACTIVITY packets of the bar ({"bar": …}); false for the other activities. */
bool bar_receive(const char *json, u64 now);
bool bar_room(void);
/** Sip pose of an entity: 1 raised, 0 down, -1 not a bar item (usual idle sipping). */
int bar_sip(const Ent *e, u64 now, bool moving);
/** "Press A" at the counter or the bartender. */
bool bar_context(char *label, int cap, u64 now);
void bar_context_run(void);
/** The bar window (menu, or the actions of what you hold). */
void bar_draw(u64 now);
bool bar_holding(void);
