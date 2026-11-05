// Client state: what the server (or the offline simulation) has
// reported. Equivalent to State in client/src/state.ts.
#pragma once
#include "common.h"

#define MAX_ENTS 128
#define MAX_ITEMS 512
#define MAX_SIDE 64
#define NAME_CAP 33
#define MAX_INV 1024
enum { CURVE_FULL=0x40, CURVE_PENDING=0x80 };

typedef struct {
  bool used;
  u8 z_fine;
  u32 id, guest;
  char name[NAME_CAP];
  u8 x, y, dir, flags, next_x, next_y;
  /** Native device (DEVICE_*), from S_DEVICE; 0 when the server did not announce one. */
  u8 device;
  u8 curve_style;
  s8 curve_overshoot;
  s16 curve_amount;
  u64 look;
  u16 z;
  u16 effect;
  /** Origin of the current step, interpolated over ent_move_ms(). */
  float fx, fy, fz;
  u64 t0, talk_until, drink_until, typing_until;
  s8 move_dir;
  u8 drink;
  /** Expression and dance style from the server's ACTION (4 kiss, 5 laugh, 6 sleep, 7 thumb up,
   *  8 sign; dance 1-4), and when the expression ends. */
  u8 action, dance;
  u32 move_ms; /* CURVE_FULL retains source duration 0..100000; legacy 1 is instant. */
  u64 action_until;
} Ent;

typedef struct {
  bool used;
  u8 z_fine;
  u16 slide_ms;
  u32 id, owner;
  u16 def, z;
  u8 x, y, rot, state;
  u8 curve_style;
  s8 curve_overshoot;
  s16 curve_amount;
  float fx, fy, fz;
  u32 move_ms;
  u64 slide_at;
  u64 state_at;
} Item;

static inline float item_z(const Item *it) {return it->z+it->z_fine/100.0f;}
static inline float ent_z(const Ent *e) {return e->z+e->z_fine/100.0f;}
static inline u32 item_height100(const Item *it) {return (u32)it->z*100+it->z_fine;}

u64 item_animation_time(const Item *it,u64 now);
u32 item_move_ms(const Item *it);
u32 ent_move_ms(const Ent *e);
bool item_sliding(const Item *it,u64 now);

typedef struct { u32 id; u16 def; } InvItem;
typedef struct {
  u32 id, players[2];
  u8 kind, turn, result, reason, last[2], check, board[64];
  u16 move_count;
  u8 moves[256][2];
} Duel;
typedef struct { u32 from; u8 kind; } GameInvite;

typedef struct {
  u32 me, guest_id, credits, owner;
  bool credits_up;
  InvItem inv[MAX_INV];
  int inv_n;
  /** Incremented on every inventory change (to refresh the interface). */
  u32 inv_rev;
  /** PETS and PET_INVENTORY JSON as received (parsed by the interface). */
  char *pets_json, *pet_inv_json, *owners_json, *rights_json;
  u32 owners_rev, rights_rev;
  /* Room rights here: may decorate (owner or rights holder), and whether we own the room. */
  bool room_rights, room_owner;
  char wall[9], floor[9];
  u32 finish_rev;
  Duel duel;
  GameInvite invites[8];
  u32 game_rev;
  u32 pets_rev;
  int online;
  char name[NAME_CAP], token[80];
  char slug[32], title[64], model[32], owner_name[NAME_CAP];
  u8 w, h, door_x, door_y;
  bool in_room;
  char rows[MAX_SIDE * MAX_SIDE];
  Ent ents[MAX_ENTS];
  Item items[MAX_ITEMS];
} World;

extern World W;

void world_handle(const u8 *data, int len);
bool world_tile(int x, int y);
/** Height of the floor in pixels: the base of the wall behind raised tiles. */
int world_floor_height(int x, int y);
bool world_placement_height100(u16 def,int x,int y,int rot,u32 ignore,int stack_mode,u32 *height100);
typedef struct {u32 height100;u64 helper_cells;} WorldPlacement;
bool world_placement(u16 def,int x,int y,int rot,u32 ignore,int stack_mode,WorldPlacement *placement);
bool world_walkable_tile(int x,int y);
bool world_supports_items(const Item *item);
Ent *world_ent(u32 id);
bool world_club_member(void);
/** Same as State.canDecorate: only the creator of a private room. */
bool world_can_decorate(void);
/** Move and rotate: own items, the owner's room, or rights in this room. */
bool world_can_arrange(const Item *it);
/** Same as State.canManage. */
bool world_can_manage(const Item *it);
/** Public slug of a room (system rooms: lobby, park, pool), like room-links.ts. */
const char *public_slug(const char *slug);
/** Is it a public room model (lobby, park, pool)? */
bool public_model(const char *model);
Item *world_item(u32 id);
/** Topmost furniture on a tile (wall items excluded). */
Item *world_item_at(int x, int y);
Item *world_pointer_item_at(int x,int y);
void ent_pos(const Ent *e, u64 now, float *x, float *y);
float ent_height(const Ent *e, u64 now);
bool ent_moving(const Ent *e, u64 now);
void item_pos(const Item *it, u64 now, float *x, float *y, float *z);

// Events implemented by the application (scene and interface).
void on_room(void);
void on_chat(u32 eid, const char *text);
void on_styled_chat(u32 eid,const char *text,s8 width,u8 flags);
void on_notice(const char *text);
/** Asked to leave the room by its owner or a rights holder (S_KICKED). */
void on_kicked(const char *text);
void on_transfer(const char *slug, const char *arrival);
void on_alert(const char *json);
void on_casino(const char *json);
/** s2c TRADE: the JSON state of the player's furniture trade. */
void on_trade(const char *json);
/** s2c ACTIVITY: the Lido activity state (JSON). */
void on_activity(const char *json);
/** s2c MUSIC: the room playlist (JSON). */
void on_music(const char *json);
void on_furniture(const char *json);
void on_snow(const u8 *data, int len);
void on_social_changed(void);
void on_name_changed(u32 eid);
/** ITEM_ADD / ITEM_UPDATE (confirmation of placing or moving). */
void on_item(u32 id);
