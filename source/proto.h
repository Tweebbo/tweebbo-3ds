// Tweebbo binary protocol (version 6). Must match
// server/src/proto.rs and client/src/proto.ts of the web edition.
#pragma once
#include "common.h"

#define PROTOCOL_VERSION 6
enum { NATIVE_STATUS = 2, NATIVE_GAMES = 4, NATIVE_CASINO = 8, NATIVE_SNOW = 16, NATIVE_SNOW2 = 32 /* 180 ms Snow Storm turns */, NATIVE_ROOMMOD = 64 };
/* Feature byte after the device byte. 1.0.8: VIP Dash with credits, furniture trading and
   Tweebbo Club badges (chips stay PC-only). */
enum { FEATURE_ECONOMY = 1 };

enum {
  C_HELLO = 1, C_WALK, C_CHAT, C_GESTURE, C_SET_NAME, C_BUY, C_PLACE, C_PICKUP,
  C_ROTATE, C_USE, C_SET_LOOK, C_MOVE, C_REDEEM, C_PET, C_DRINK, C_GAME, C_SNOW, C_DECORATE, C_CASINO, C_ROOM_MOD, C_TRADE,
};
/* Room rights (JSON) and being asked to leave a room (server room/moderation.rs). */
enum { S_ROOM_RIGHTS = 136, S_KICKED = 137 };
/* Native device of a player: var count, then {var entity id, u8 device}. Added for cross-play
   (3DS, Nintendo Switch, PlayStation Vita); older servers never send it. */
enum { S_DEVICE = 140 };
/* Chips balance (PC-only currency, ignored here) and a furniture trade (JSON, server room/trade.rs). */
enum { S_CHIPS = 138, S_TRADE = 139 };
enum { DEVICE_BROWSER = 0, DEVICE_3DS = 1, DEVICE_SWITCH = 2, DEVICE_VITA = 3 };
/* ROOM_MOD operations. */
enum { MOD_GIVE_RIGHTS = 1, MOD_REMOVE_RIGHTS, MOD_REMOVE_ALL, MOD_KICK, MOD_BAN, MOD_UNBAN, MOD_REMOVE_RIGHTS_OF };
enum {
  S_WELCOME = 101, S_ROOM, S_ENTITY_ADD, S_ENTITY_REMOVE, S_DELTAS, S_CHAT, S_CATALOGUE,
  S_INVENTORY, S_CREDITS, S_ITEM_ADD, S_ITEM_REMOVE, S_ITEM_UPDATE, S_NOTICE, S_INV_ADD,
  S_INV_REMOVE, S_ENTITY_NAME, S_ENTITY_LOOK, S_SLIDE, S_TRANSFER, S_PETS, S_PET_INVENTORY,
  S_SOCIAL_CHANGED, S_ONLINE, S_CARRY, S_HOTEL_ALERT, S_REPLACED,
};
enum { S_GAME = 127, S_SNOW, S_LOCKED, S_FINISH, S_CASINO, S_CAPACITY, S_MODERATION, S_OWNER_IDENTITY };
/** FLAG_3DS: player connected from the console (wire-only, added by the server). */
/** FLAG_CLUB: Tweebbo Club member (wire flag). */
enum { FLAG_CLUB = 1, FLAG_SIT = 2, FLAG_WAVE = 4, FLAG_DANCE = 8, FLAG_LAY = 16, FLAG_ROLL = 32, FLAG_3DS = 64 };
enum { GESTURE_WAVE = 1, GESTURE_DANCE = 2, GESTURE_SIT = 3 };

#define MOVE_MS 500

typedef struct { u8 *buf; int len, cap; u8 data[1024]; } Writer;
void w_begin(Writer *w, u8 kind);
/** Like w_begin, but writes into an external buffer (large messages). */
void w_begin_into(Writer *w, u8 kind, u8 *buf, int cap);
void w_u8(Writer *w, u32 v);
void w_u16(Writer *w, u32 v);
void w_var(Writer *w, u64 v);
void w_str(Writer *w, const char *s);

typedef struct { const u8 *b; int len, p; bool err; } Reader;
void r_init(Reader *r, const u8 *b, int len);
u8 r_u8(Reader *r);
u16 r_u16(Reader *r);
u64 r_var(Reader *r);
/** Copies the string, truncating it to `cap - 1` bytes without splitting a UTF-8 character. */
void r_str(Reader *r, char *out, int cap);
