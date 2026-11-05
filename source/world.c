#include "world.h"
#include "lookup_cache.h"
#include "catalog.h"
#include "proto.h"
#include "height_wire.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(Ent)<=128&&sizeof(Item)<=56,"Native movement must preserve entity and item memory budgets");
_Static_assert(MAX_SIDE<=64,"Packed heights use the existing 64-tile coordinate limit");
extern bool wired_visuals_click_through(bool user,u32 id) __attribute__((weak));
extern void wired_visuals_reset(void) __attribute__((weak));
extern void wired_visuals_forget(bool user,u32 id) __attribute__((weak));
World W;
__attribute__((weak)) void on_styled_chat(u32 eid,const char *text,s8 width,u8 flags){(void)width;(void)flags;on_chat(eid,text);}
static LookupCache entity_lookup;

bool world_tile(int x, int y) {
  return x >= 0 && y >= 0 && x < W.w && y < W.h && W.rows[y * W.w + x] != 'x';
}

bool world_club_member(void) {
  const Ent *me=world_ent(W.me);return me&&(me->flags&FLAG_CLUB);
}

int world_floor_height(int x, int y) {
  if (!world_tile(x, y)) return 0;
  char level = W.rows[y * W.w + x];
  if (level >= '0' && level <= '9') return (level - '0') * 16;
  return level >= 'a' && level <= 'z' ? (level - 'a' + 10) * 16 : 0;
}

Ent *world_ent(u32 id) {
  int cached=lookup_cache_get(&entity_lookup,id);
  if(cached>=0&&cached<MAX_ENTS&&W.ents[cached].used&&W.ents[cached].id==id)return &W.ents[cached];
  for (int i = 0; i < MAX_ENTS; i++) if (W.ents[i].used && W.ents[i].id == id) {lookup_cache_put(&entity_lookup,id,(unsigned)i);return &W.ents[i];}
  return NULL;
}

const char *public_slug(const char *slug) {
  // System rooms keep internal ids on the server; their public names follow the room model.
  if (W.owner == 0 && !strcmp(slug, W.slug)) {
    if (!strcmp(W.model, "newbie_lobby")) return "lobby";
    if (!strcmp(W.model, "park_a")) return "park";
    if (!strcmp(W.model, "pool_a")) return "pool";
  }
  return slug;
}

bool public_model(const char *model) {
  return !strcmp(model, "newbie_lobby") || !strcmp(model, "park_a") || !strcmp(model, "pool_a") || !strcmp(model, "casino");
}

bool world_can_decorate(void) { return W.owner && (W.owner == W.guest_id || W.room_rights) && !public_model(W.model); }
bool world_can_arrange(const Item *it) { return world_can_manage(it) || (W.room_rights && world_can_decorate()); }
bool world_can_manage(const Item *it) { return it->owner == W.guest_id || W.owner == W.guest_id; }

static void inv_add(u32 id, u16 def) {
  for (int i = 0; i < W.inv_n; i++) if (W.inv[i].id == id) { W.inv[i].def = def; W.inv_rev++; return; }
  if (W.inv_n < MAX_INV) W.inv[W.inv_n++] = (InvItem){id, def};
  W.inv_rev++;
}

static void inv_remove(u32 id) {
  for (int i = 0; i < W.inv_n; i++)
    if (W.inv[i].id == id) { memmove(&W.inv[i], &W.inv[i + 1], sizeof(InvItem) * (size_t)(W.inv_n - i - 1)); W.inv_n--; break; }
  W.inv_rev++;
}

static void set_json(char **dst, Reader *r) {
  int n = (int)r_var(r);
  if (r->err || n < 0 || r->p + n > r->len) { r->err = true; return; }
  char *s = malloc((size_t)n + 1);
  if (!s) return;
  memcpy(s, r->b + r->p, (size_t)n);
  s[n] = 0;
  r->p += n;
  free(*dst);
  *dst = s;
  W.pets_rev++;
}

Item *world_item(u32 id) {
  for (int i = 0; i < MAX_ITEMS; i++) if (W.items[i].used && W.items[i].id == id) return &W.items[i];
  return NULL;
}

extern bool wired_clicks_furniture_through(void) __attribute__((weak));
static Item *item_at(int x,int y,bool pointer) {
  Item *best = NULL;const FurniDef *best_def=NULL;
  for (int i = 0; i < MAX_ITEMS; i++) {
    Item *it = &W.items[i];
    if (!it->used||(pointer&&((wired_clicks_furniture_through&&wired_clicks_furniture_through())||(wired_visuals_click_through&&wired_visuals_click_through(false,it->id))))) continue;
    const FurniDef *d = def_get(it->def);
    if (!d || d->kind == KIND_WALL) continue;
    int w, h;
    def_size(it->def, it->rot, &w, &h);
    if (x < it->x || y < it->y || x >= it->x + w || y >= it->y + h) continue;
    bool walk=def_height_helper(d)==2,best_walk=def_height_helper(best_def)==2;
    bool higher=!best;
    if(best){
      if(walk!=best_walk)higher=walk;
      else if(item_height100(it)!=item_height100(best))higher=item_height100(it)>item_height100(best);
      else if((d->kind!=KIND_FLOOR)!=(best_def->kind!=KIND_FLOOR))higher=d->kind!=KIND_FLOOR;
      else if(d->kind==KIND_FLOOR&&d->seat!=best_def->seat)higher=d->seat>best_def->seat;
      else higher=it->id>best->id;
    }
    if(higher){best=it;best_def=d;}
  }
  return best;
}
Item *world_item_at(int x,int y){return item_at(x,y,false);}
Item *world_pointer_item_at(int x,int y){return item_at(x,y,true);}
bool world_walkable_tile(int x,int y){
  if(x<0||y<0||x>=W.w||y>=W.h)return false;
  if(world_tile(x,y))return true;
  const Item *item=world_item_at(x,y);return item&&def_height_helper(def_get(item->def))==2;
}

bool world_supports_items(const Item *item) {
  const FurniDef *d=def_get(item->def);if(!d||d->kind==KIND_WALL||def_height_helper(d)||!d->seat)return false;
  int w,h;def_size(item->def,item->rot,&w,&h);u32 top=item_height100(item)+d->seat*100u;
  for(int i=0;i<MAX_ITEMS;i++){
    const Item *other=&W.items[i];
    if(!other->used||other->id==item->id||item_height100(other)<top)continue;
    const FurniDef *support=def_get(other->def);if(!support||support->kind==KIND_WALL)continue;
    int ow,oh;def_size(other->def,other->rot,&ow,&oh);
    if(other->x<item->x+w&&other->y<item->y+h&&other->x+ow>item->x&&other->y+oh>item->y)return true;
  }
  return false;
}
bool world_placement(u16 def,int x,int y,int rot,u32 ignore,int stack_mode,WorldPlacement *placement) {
  const FurniDef *d=def_get(def);if(!d||d->kind==KIND_WALL)return false;
  int w,h;def_size(def,(u8)rot,&w,&h);
  if(x<0||y<0||w<=0||h<=0||w*h>64||x+w>W.w||y+h>W.h)return false;
  u32 surfaces[64],floor=0;u64 helpers=0,blocked=0;bool magic=def_height_helper(d)!=0;
  for(int dy=0;dy<h;dy++)for(int dx=0;dx<w;dx++){
    int tx=x+dx,ty=y+dy;if(tx==W.door_x&&ty==W.door_y)return false;
    surfaces[dy*w+dx]=(u32)world_floor_height(tx,ty)*100u;
    if(surfaces[dy*w+dx]>floor)floor=surfaces[dy*w+dx];
  }
  if(stack_mode!=2||magic)for(int i=0;i<MAX_ITEMS;i++){
    const Item *it=&W.items[i];if(!it->used||it->id==ignore)continue;
    const FurniDef *support=def_get(it->def);if(!support||support->kind==KIND_WALL)continue;
    int ow,oh;def_size(it->def,it->rot,&ow,&oh);
    int x0=it->x>x?it->x:x,y0=it->y>y?it->y:y,x1=it->x+ow<x+w?it->x+ow:x+w,y1=it->y+oh<y+h?it->y+oh:y+h;
    if(x0>=x1||y0>=y1)continue;
    bool helper=def_height_helper(support)!=0;
    u32 top=item_height100(it)+(helper?0:support->seat*100u);
    for(int ty=y0;ty<y1;ty++)for(int tx=x0;tx<x1;tx++){
      int cell=(ty-y)*w+tx-x;u64 bit=1ull<<cell;
      if(helper){if(!(helpers&bit)||top>surfaces[cell])surfaces[cell]=top;helpers|=bit;}
      else if(!(helpers&bit)&&top>surfaces[cell])surfaces[cell]=top;
      if(support->kind!=KIND_FLOOR&&!(support->flags&DEF_STACK))blocked|=bit;
    }
  }
  u32 height=surfaces[0];bool uneven=false;
  for(int cell=0;cell<w*h;cell++){
    if(!magic&&!(helpers&(1ull<<cell))&&!world_tile(x+cell%w,y+cell/w))return false;
    if(surfaces[cell]!=surfaces[0])uneven=true;
    if(surfaces[cell]>height)height=surfaces[cell];
  }
  if(!magic&&!stack_mode&&((blocked&~helpers)||(uneven&&!helpers)))return false;
  if(magic){u32 maximum=floor>128000?floor:128000;if(height<floor)height=floor;if(height>maximum)height=maximum;}
  else if(height>=(helpers?256000u:25600u))return false;
  *placement=(WorldPlacement){.height100=height,.helper_cells=helpers};return true;
}
bool world_placement_height100(u16 def,int x,int y,int rot,u32 ignore,int stack_mode,u32 *height100) {
  WorldPlacement placement;if(!world_placement(def,x,y,rot,ignore,stack_mode,&placement))return false;
  *height100=placement.height100;return true;
}

u32 ent_move_ms(const Ent *e) {if(e->curve_style&CURVE_FULL)return e->move_ms;return e->move_ms==1?0:e->move_ms>=50&&e->move_ms<=2000?e->move_ms:MOVE_MS;}

static float progress(const Ent *e, u64 now) {
  if(e->curve_style&CURVE_PENDING)return 0;
  u64 t0=e->t0;u32 duration=ent_move_ms(e);
  if (!duration) return 1;
  u64 elapsed=now>=t0?now-t0:0;
  if((e->curve_style&CURVE_FULL)&&elapsed==(duration>>1))elapsed++;
  float t = (float)elapsed / duration;
  return t > 1 ? 1 : t;
}

void ent_pos(const Ent *e, u64 now, float *x, float *y) {
  float t = progress(e, now);
  // The door is the threshold in the wall, not the center of a tile.
  float shift = world_tile(W.door_x, W.door_y) ? -.5f : .5f;
  float from = e->fx + (e->fx == W.door_x && e->fy == W.door_y ? shift : 0);
  float to = e->x + (e->x == W.door_x && e->y == W.door_y ? shift : 0);
  *x = from + (to - from) * t;
  *y = e->fy + (e->y - e->fy) * t;
}

float ent_height(const Ent *e, u64 now) {
  float t=progress(e,now),dz=ent_z(e)-e->fz;
  float jump=(e->curve_style&7)==7?e->curve_amount*.32f*sqrtf((e->x-e->fx)*(e->x-e->fx)+(e->y-e->fy)*(e->y-e->fy)+(dz/32)*(dz/32))*t*(1-t):0;
  return e->fz+dz*t+jump;
}

bool ent_moving(const Ent *e, u64 now) {
  return !(e->curve_style&CURVE_PENDING)&&now < e->t0 + ent_move_ms(e) && (e->fx != e->x || e->fy != e->y);
}

static void read_item(Reader *r, Item *it,bool precise) {
  *it = (Item){.used = true};
  it->id = (u32)r_var(r);
  it->def = r_u16(r);
  it->x = r_u8(r); it->y = r_u8(r); it->rot = r_u8(r); it->state = r_u8(r);
  it->owner = (u32)r_var(r);
  it->z = r_u16(r);
  it->z_fine=height_item_fields(r,&it->x,it->y,&it->rot,precise);
}

static void read_ent(Reader *r, Ent *e,bool precise) {
  memset(e, 0, sizeof *e);
  e->used = true;
  e->id = (u32)r_var(r);
  r_str(r, e->name, sizeof e->name);
  e->x = r_u8(r); e->y = r_u8(r); e->dir = r_u8(r); e->flags = r_u8(r);
  e->look = r_var(r);
  e->z = r_u16(r);
  e->guest = (u32)r_var(r);
  e->z_fine=height_entity_fields(r,&e->x,e->y,&e->dir,precise);
  e->fx = e->x; e->fy = e->y; e->fz = ent_z(e);
  e->next_x = e->next_y = 255;
  e->move_dir = -1;
}

static Ent *new_ent(void) {
  for (int i = 0; i < MAX_ENTS; i++) if (!W.ents[i].used) return &W.ents[i];
  return NULL;
}

static Item *put_item(const Item *src) {
  Item *it = world_item(src->id);
  for (int i = 0; !it && i < MAX_ITEMS; i++) if (!W.items[i].used) it = &W.items[i];
  if (it) {
    if(wired_visuals_forget&&(!it->used||it->def!=src->def||it->owner!=src->owner))wired_visuals_forget(false,src->id);
    Item next = *src;
    next.state_at=it->used && it->state==src->state?it->state_at:now_ms();
    if (it->used && !next.slide_at && it->x == next.x && it->y == next.y && it->z == next.z && it->z_fine == next.z_fine) {
      next.fx = it->fx; next.fy = it->fy; next.fz = it->fz; next.slide_at = it->slide_at; next.slide_ms = it->slide_ms; next.move_ms=it->move_ms;next.curve_style=it->curve_style;next.curve_amount=it->curve_amount;next.curve_overshoot=it->curve_overshoot;
    }
    *it = next;
  }
  return it;
}

u64 item_animation_time(const Item *it,u64 now) {
  const FurniDef *definition=def_get(it->def);
  return definition && (definition->use_ticks || definition->use_random) && it->state_at && now>=it->state_at?now-it->state_at:now;
}

u32 item_move_ms(const Item *it) {if(it->curve_style&CURVE_FULL)return it->move_ms;return it->slide_ms>=50&&it->slide_ms<=2000?it->slide_ms:MOVE_MS;}
bool item_sliding(const Item *it,u64 now) {return !(it->curve_style&CURVE_PENDING)&&it->slide_at&&now>=it->slide_at&&now-it->slide_at<item_move_ms(it);}
void item_pos(const Item *it, u64 now, float *x, float *y, float *z) {
  u32 duration=item_move_ms(it);
  u64 elapsed=now>=it->slide_at?now-it->slide_at:0;
  if((it->curve_style&CURVE_FULL)&&elapsed==(duration>>1))elapsed++;
  float t=(it->curve_style&CURVE_PENDING)?0:it->slide_at&&duration?fminf(1,(float)elapsed/duration):1;
  *x = it->fx + (it->x - it->fx) * t;
  *y = it->fy + (it->y - it->fy) * t;
  float dz=item_z(it)-it->fz;
  float jump=(it->curve_style&7)==7?it->curve_amount*.32f*sqrtf((it->x-it->fx)*(it->x-it->fx)+(it->y-it->fy)*(it->y-it->fy)+(dz/32)*(dz/32))*t*(1-t):0;
  *z = it->fz+dz*t+jump;
}

static bool precise_room_valid(Reader scan) {
  char discard[1];
  for(int i=0;i<3;i++)r_str(&scan,discard,sizeof discard);
  u8 w=r_u8(&scan),h=r_u8(&scan);if(!w||!h||w>MAX_SIDE||h>MAX_SIDE)return false;
  r_str(&scan,discard,sizeof discard);r_u8(&scan);r_u8(&scan);r_var(&scan);r_str(&scan,discard,sizeof discard);
  u64 count=r_var(&scan);if(count>4096)return false;
  for(unsigned i=0;i<count&&!scan.err;i++){Item it;read_item(&scan,&it,true);}
  count=r_var(&scan);if(count>4096)return false;
  for(unsigned i=0;i<count&&!scan.err;i++){Ent e;read_ent(&scan,&e,true);}
  return !scan.err&&scan.p==scan.len;
}

void world_handle(const u8 *data, int len) {
  Reader r;
  r_init(&r, data, len);
  u64 now = now_ms();
  char text[2049];
  u8 kind=r_u8(&r);bool precise=height_packet_precise(kind);
  switch (height_packet_base(kind)) {
    case S_WELCOME:
      W.me = (u32)r_var(&r);
      r_str(&r, W.name, sizeof W.name);
      r_str(&r, W.token, sizeof W.token);
      W.credits = (u32)r_var(&r);
      W.guest_id = (u32)r_var(&r);
      break;
    case S_ROOM_TITLE: r_str(&r, W.title, sizeof W.title); break;
    case S_ROOM: {
      if(precise&&!precise_room_valid(r))break;
      W.room_rights = W.room_owner = false;
      r_str(&r, W.slug, sizeof W.slug);
      r_str(&r, W.title, sizeof W.title);
      r_str(&r, W.model, sizeof W.model);
      W.w = r_u8(&r); W.h = r_u8(&r);
      static char rows[MAX_SIDE * (MAX_SIDE + 1) + 1];
      r_str(&r, rows, sizeof rows);
      memset(W.rows, 'x', sizeof W.rows);
      if (W.w > MAX_SIDE) W.w = MAX_SIDE;
      if (W.h > MAX_SIDE) W.h = MAX_SIDE;
      const char *p = rows;
      for (int y = 0; y < W.h && *p; y++) {
        for (int x = 0; x < W.w && *p && *p != '\n'; x++) W.rows[y * W.w + x] = *p++;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
      }
      W.door_x = r_u8(&r); W.door_y = r_u8(&r);
      W.owner = (u32)r_var(&r);
      r_str(&r, W.owner_name, sizeof W.owner_name);
      if(wired_visuals_reset)wired_visuals_reset();
      memset(W.items, 0, sizeof W.items);
      for (int n = (int)r_var(&r), i = 0; n > 0 && !r.err; n--) {
        Item it; read_item(&r, &it,precise);
        if (i < MAX_ITEMS) W.items[i++] = it;
      }
      memset(W.ents, 0, sizeof W.ents);
      for (int n = (int)r_var(&r), i = 0; n > 0 && !r.err; n--) {
        Ent e; read_ent(&r, &e,precise);
        if (i < MAX_ENTS) W.ents[i++] = e;
      }
      W.wall[0] = W.floor[0] = 0;
      memset(W.invites, 0, sizeof W.invites);
      memset(&W.duel, 0, sizeof W.duel);
      W.game_rev++;
      W.in_room = true;
      on_room();
      break;
    }
    case S_ENTITY_ADD: {
      Ent e; read_ent(&r, &e,precise);if(r.err)break;
      Ent *old = world_ent(e.id), *slot = old ? old : new_ent();
      if(slot){if(wired_visuals_forget)wired_visuals_forget(true,e.id);*slot=e;}
      break;
    }
    case S_ENTITY_REMOVE: {
      Ent *e = world_ent((u32)r_var(&r));
      if(e){if(wired_visuals_forget)wired_visuals_forget(true,e->id);e->used=false;}
      break;
    }
    case S_TYPING: {
      Ent *e=world_ent((u32)r_var(&r));
      u8 active=r_u8(&r);
      if(e&&!r.err)e->typing_until=active?now+8000:0;
      break;
    }
    case S_EFFECT:{
      u32 count=r_var(&r);if(count>4096)break;
      for(u32 i=0;i<count&&!r.err;i++){u32 id=r_var(&r);u16 effect=r_u16(&r);Ent *e=world_ent(id);if(e&&!r.err)e->effect=effect;}
      break;
    }
    case S_ACTION:
      // Expressions and dance styles: id, action, dance style, remaining milliseconds.
      for (int n = (int)r_var(&r); n > 0 && !r.err; n--) {
        Ent *e = world_ent((u32)r_var(&r));
        u8 action = r_u8(&r), dance = r_u8(&r);
        u32 left = (u32)r_var(&r);
        if (e && !r.err) { e->action = action; e->dance = dance; e->action_until = left ? now + left : 0; }
      }
      break;
    case S_DEVICE:
      // Native device of each player (3DS/Switch/Vita); old servers never send it.
      for (int n = (int)r_var(&r); n > 0 && !r.err; n--) {
        Ent *e = world_ent((u32)r_var(&r));
        u8 device = r_u8(&r);
        if (e && !r.err) e->device = device;
      }
      break;
    case S_DELTAS: {
      u64 count=r_var(&r);if(r.err||count>4096)break;
      Reader scan=r;
      for(unsigned i=0;i<count&&!scan.err;i++) {
        if(r_var(&scan)>UINT32_MAX)scan.err=true;
        u8 sx=r_u8(&scan),sy=r_u8(&scan),sd=r_u8(&scan);height_entity_fields(&scan,&sx,sy,&sd,precise);
        for(int j=0;j<3;j++){r_u8(&scan);}r_u16(&scan);
      }
      int remaining=scan.len-scan.p;
      u16 duration=remaining==2?r_u16(&scan):MOVE_MS;
      if(scan.err||(remaining!=0&&remaining!=2)||(duration!=0&&(duration<50||duration>2000)))break;
      for (unsigned i=0;i<count;i++) {
        u32 id = (u32)r_var(&r);
        u8 x = r_u8(&r), y = r_u8(&r), dir = r_u8(&r), flags = r_u8(&r);
        u8 nx = r_u8(&r), ny = r_u8(&r);
        u16 z = r_u16(&r);u8 fine=height_entity_fields(&r,&x,y,&dir,precise);
        Ent *e = world_ent(id);
        if (!e) continue;
        if ((e->curve_style&CURVE_PENDING)||e->x != x || e->y != y || e->z != z || e->z_fine != fine) {
          e->move_dir = (s8)(((int)lroundf(atan2f((float)(x - e->x), (float)-(y - e->y)) / ((float)M_PI / 4)) + 8) & 7);
          float px, py;
          ent_pos(e, now, &px, &py);
          e->fz = ent_height(e, now);
          e->fx = px; e->fy = py; e->t0 = now;
          if(e->curve_style&CURVE_PENDING)e->curve_style&=~CURVE_PENDING;
          else{e->curve_style=0;e->curve_amount=0;e->curve_overshoot=0;e->move_ms=duration?duration:1;}
          e->x = x; e->y = y; e->z = z; e->z_fine=fine;
        }
        if(!duration&&!(e->curve_style&CURVE_FULL)){e->fx=x;e->fy=y;e->fz=z+fine/100.0f;e->move_ms=1;e->t0=now;}
        e->dir = dir; e->flags = flags; e->next_x = nx; e->next_y = ny;
      }
      break;
    }
    case S_MOVE_STYLE: {
      u64 id=r_var(&r);u8 avatar=r_u8(&r),style=r_u8(&r);s16 amount=(s16)r_u16(&r),overshoot=(s16)r_u16(&r);u64 duration=r_var(&r);
      if(r.err||r.p!=r.len||id>UINT32_MAX||avatar>1||(style!=0&&style!=7)||duration>100000||overshoot!=0||(style==0&&(amount<0||amount>100))||(style==7&&(amount< -1000||amount>1000)))break;
      if(avatar){Ent *e=world_ent((u32)id);if(e){float x,y,z=ent_height(e,now);ent_pos(e,now,&x,&y);e->fx=x;e->fy=y;e->fz=z;e->t0=now;e->curve_style=style|CURVE_FULL|CURVE_PENDING;e->curve_amount=amount;e->curve_overshoot=(s8)overshoot;e->move_ms=(u32)duration;}}
      else{Item *it=world_item((u32)id);if(it){float x,y,z;item_pos(it,now,&x,&y,&z);it->fx=x;it->fy=y;it->fz=z;it->slide_at=now;it->curve_style=style|CURVE_FULL|CURVE_PENDING;it->curve_amount=amount;it->curve_overshoot=(s8)overshoot;it->move_ms=(u32)duration;}}
      break;
    }
    case S_STYLED_CHAT: {
      u64 eid=r_var(&r);r_str(&r,text,sizeof text);s8 width=(s8)r_u8(&r);u8 flags=r_u8(&r);
      if(!r.err&&r.p==r.len&&eid<=UINT32_MAX&&width>=-1&&width<=2&&!(flags&~3))on_styled_chat((u32)eid,text,width,flags);
      break;
    }
    case S_CHAT: {
      u32 eid = (u32)r_var(&r);
      r_str(&r, text, sizeof text);
      if (!r.err) on_chat(eid, text);
      break;
    }
    case S_CREDITS: { u32 before = W.credits; W.credits = (u32)r_var(&r); if (W.credits > before) W.credits_up = true; W.inv_rev++; break; }
    case S_INVENTORY:
      W.inv_n = 0;
      for (int n = (int)r_var(&r); n > 0 && !r.err; n--) {
        u32 id = (u32)r_var(&r);
        u16 def = r_u16(&r);
        if (!r.err && W.inv_n < MAX_INV) W.inv[W.inv_n++] = (InvItem){id, def};
      }
      W.inv_rev++;
      break;
    case S_INV_ADD: { u32 id = (u32)r_var(&r); u16 def = r_u16(&r); if (!r.err) inv_add(id, def); break; }
    case S_INV_REMOVE: { u32 id = (u32)r_var(&r); if (!r.err) inv_remove(id); break; }
    case S_TRANSFER: {
      char slug[64], arrival[80];
      r_str(&r, slug, sizeof slug);
      r_str(&r, arrival, sizeof arrival);
      if (!r.err) on_transfer(public_slug(slug), arrival);
      break;
    }
    case S_HOTEL_ALERT: {
      // Alerts allow 1000 characters (up to ~4 KB of escaped UTF-8 JSON).
      static char alert[8192];
      r_str(&r, alert, sizeof alert);
      if (!r.err) on_alert(alert);
      break;
    }
    case S_SOCIAL_CHANGED: on_social_changed(); break;
    case S_PETS: set_json(&W.pets_json, &r); break;
    case S_PET_INVENTORY: set_json(&W.pet_inv_json, &r); break;
    case S_GAME: {
      u8 op = r_u8(&r);
      if (op == 1 || op == 4) {
        u32 from = (u32)r_var(&r); u8 kind = op == 1 ? r_u8(&r) : 0;
        if (r.err || (op == 1 && (kind < 1 || kind > 3))) break;
        int slot = -1;
        for (int i = 0; i < 8; i++) if (W.invites[i].from == from) { slot = i; break; }
        if (slot < 0 && op == 1) for (int i = 0; i < 8; i++) if (!W.invites[i].from) { slot = i; break; }
        if (slot >= 0) W.invites[slot] = (GameInvite){op == 1 ? from : 0, kind};
      } else if (op == 2) {
        Duel game = {0};
        game.id = (u32)r_var(&r); game.kind = r_u8(&r);
        game.players[0] = (u32)r_var(&r); game.players[1] = (u32)r_var(&r);
        game.turn = r_u8(&r); game.result = r_u8(&r); game.reason = r_u8(&r);
        game.last[0] = r_u8(&r); game.last[1] = r_u8(&r); game.check = r_u8(&r);
        u64 n = r_var(&r);
        if (game.kind < 1 || game.kind > 3 || game.turn > 1 || game.result > 3 || n != (game.kind == 1 ? 9u : game.kind == 2 ? 42u : 64u)) break;
        for (unsigned i = 0; i < n; i++) game.board[i] = r_u8(&r);
        u64 moves = r_var(&r);
        if (moves > 256) break;
        game.move_count = (u16)moves;
        for (unsigned i = 0; i < moves; i++) { game.moves[i][0] = r_u8(&r); game.moves[i][1] = r_u8(&r); }
        if (r.err) break;
        W.duel = game;
      } else break;
      W.game_rev++;
      break;
    }
    case S_OWNER_IDENTITY: set_json(&W.owners_json, &r); W.owners_rev++; break;
    case S_ROOM_RIGHTS: {
      set_json(&W.rights_json, &r);
      // Compact JSON from the server; the lists are read by the room rights window.
      W.room_rights = W.rights_json && strstr(W.rights_json, "\"rights\":true");
      W.room_owner = W.rights_json && strstr(W.rights_json, "\"owner\":true");
      W.rights_rev++;
      break;
    }
    case S_KICKED: r_str(&r, text, sizeof text); if (!r.err) on_kicked(text); break;
    case S_CASINO: {
      char *event = NULL; set_json(&event, &r);
      if (event) { on_casino(event); free(event); }
      break;
    }
    case S_TRADE: {
      char *event = NULL; set_json(&event, &r);
      if (event) { on_trade(event); free(event); }
      break;
    }
    case S_FURNITURE: {
      char *event=NULL;set_json(&event,&r);if(event){on_furniture(event);free(event);}break;
    }
    case S_MUSIC: {
      char *event = NULL; set_json(&event, &r);
      if (event) { on_music(event); free(event); }
      break;
    }
    case S_ACTIVITY: {
      char *event = NULL; set_json(&event, &r);
      if (event) { on_activity(event); free(event); }
      break;
    }
    case S_SNOW: on_snow(r.b+r.p,r.len-r.p); break;
    case S_FINISH:
      r_str(&r, W.wall, sizeof W.wall);
      r_str(&r, W.floor, sizeof W.floor);
      W.finish_rev++;
      break;
    case S_ONLINE: W.online = (int)r_var(&r); break;
    case S_ITEM_ADD:
    case S_ITEM_UPDATE: { Item it; read_item(&r, &it,precise); if (!r.err) { put_item(&it); on_item(it.id); } break; }
    case S_SLIDE: {
      u64 count=r_var(&r);if(r.err||count>MAX_ITEMS)break;
      Reader scan=r;Item scratch;
      for(unsigned i=0;i<count&&!scan.err;i++)read_item(&scan,&scratch,precise);
      int remaining=scan.len-scan.p;
      u16 duration=remaining==2?r_u16(&scan):MOVE_MS;
      if(!scan.err&&(remaining==0||remaining==2)&&duration>=50&&duration<=2000) {
        for(unsigned i=0;i<count;i++) {
          Item item;read_item(&r,&item,precise);Item *it=&item;const Item *old=world_item(it->id);it->slide_ms=duration;
          if(old){item_pos(old,now,&it->fx,&it->fy,&it->fz);it->slide_at=now;if(old->curve_style&CURVE_PENDING){it->curve_style=old->curve_style&~CURVE_PENDING;it->curve_amount=old->curve_amount;it->curve_overshoot=old->curve_overshoot;it->move_ms=old->move_ms;}}
          put_item(it);
        }
      }
      break;
    }
    case S_ITEM_REMOVE: { Item *it = world_item((u32)r_var(&r)); if(it){if(wired_visuals_forget)wired_visuals_forget(false,it->id);it->used=false;} break; }
    case S_NOTICE: r_str(&r, text, sizeof text); if (!r.err) on_notice(text); break;
    case S_ENTITY_NAME: {
      Ent *e = world_ent((u32)r_var(&r));
      r_str(&r,text,NAME_CAP);
      if (e && !r.err) {
        snprintf(e->name, sizeof e->name, "%s", text);
        if (e->id == W.me) snprintf(W.name, sizeof W.name, "%s", text);
        if (e->guest == W.owner) snprintf(W.owner_name, sizeof W.owner_name, "%s", text);
        on_name_changed(e->id);
      }
      break;
    }
    case S_ENTITY_LOOK: {
      Ent *e = world_ent((u32)r_var(&r));
      u64 look = r_var(&r);
      if (e && !r.err) e->look = look;
      break;
    }
    case S_CARRY: {
      Ent *e = world_ent((u32)r_var(&r));
      u8 drink = r_u8(&r);
      u64 duration = r_var(&r);
      if (e && !r.err) { e->drink = drink; e->drink_until = now + duration; }
      break;
    }
    default:
      // Messages for features the 3DS does not show (server catalogue list…).
      break;
  }
}
__attribute__((weak)) void on_furniture(const char *json){(void)json;}
