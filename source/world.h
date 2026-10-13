// Client state: what the server (or the offline simulation) has
// reported. Equivalent to State in client/src/state.ts.
#pragma once
#include "common.h"

#define MAX_ENTS 64
#define MAX_ITEMS 512
#define MAX_SIDE 64
#define NAME_CAP 24
#define MAX_INV 1024

typedef struct {
  bool used;
  u32 id, guest;
  char name[NAME_CAP];
  u8 x, y, dir, flags, next_x, next_y;
  u64 look;
  u16 z;
  /** Origin of the current step, interpolated over MOVE_MS. */
  float fx, fy, fz;
  u64 t0, talk_until, drink_until;
  s8 move_dir;
  u8 drink;
} Ent;

typedef struct {
  bool used;
  u32 id, owner;
  u16 def, z;
  u8 x, y, rot, state;
} Item;

typedef struct { u32 id; u16 def; } InvItem;

typedef struct {
  u32 me, guest_id, credits, owner;
  InvItem inv[MAX_INV];
  int inv_n;
  /** Incremented on every inventory change (to refresh the interface). */
  u32 inv_rev;
  /** PETS and PET_INVENTORY JSON as received (parsed by the interface). */
  char *pets_json, *pet_inv_json;
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
Ent *world_ent(u32 id);
/** Same as State.canDecorate: only the creator of a private room. */
bool world_can_decorate(void);
/** Same as State.canManage. */
bool world_can_manage(const Item *it);
/** Public slug of a room (system rooms: lobby, park, pool), like room-links.ts. */
const char *public_slug(const char *slug);
/** Is it a public room model (lobby, park, pool)? */
bool public_model(const char *model);
Item *world_item(u32 id);
/** Topmost furniture on a tile (wall items excluded). */
Item *world_item_at(int x, int y);
void ent_pos(const Ent *e, u64 now, float *x, float *y);
float ent_height(const Ent *e, u64 now);
bool ent_moving(const Ent *e, u64 now);

// Events implemented by the application (scene and interface).
void on_room(void);
void on_chat(u32 eid, const char *text);
void on_notice(const char *text);
void on_transfer(const char *slug, const char *arrival);
void on_alert(const char *json);
void on_social_changed(void);
void on_name_changed(u32 eid);
/** ITEM_ADD / ITEM_UPDATE (confirmation of placing or moving). */
void on_item(u32 id);
