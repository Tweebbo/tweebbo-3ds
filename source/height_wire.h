#pragma once
#include "proto.h"

static inline bool height_packet_precise(u8 kind) {return kind>=S_ROOM_PRECISE&&kind<=S_SLIDE_PRECISE;}
static inline u8 height_packet_base(u8 kind) {
  static const u8 base[]={S_ROOM,S_ENTITY_ADD,S_DELTAS,S_ITEM_ADD,S_ITEM_UPDATE,S_SLIDE};
  return height_packet_precise(kind)?base[kind-S_ROOM_PRECISE]:kind;
}
static inline u8 height_item_fields(Reader *r,u8 *x,u8 y,u8 *rot,bool precise) {
  if(!precise)return 0;
  u8 fine=(*rot>>2)|(*x&64);
  if(fine>=100||(*x&128)||y>=64)r->err=true;
  *x&=63;*rot&=3;return fine;
}
static inline u8 height_entity_fields(Reader *r,u8 *x,u8 y,u8 *dir,bool precise) {
  if(!precise)return 0;
  u8 fine=(*dir>>3)|((*x>>6)<<5);
  if(fine>=100||y>=64)r->err=true;
  *x&=63;*dir&=7;return fine;
}
