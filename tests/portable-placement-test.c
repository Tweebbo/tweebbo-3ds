#include "../../switch/source/client_build.h"
#include "../../switch/source/catalog.h"
#include "../../switch/source/world.h"
#include "../../switch/source/pets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

u64 now_ms(void){return 1000;}
int pets_count(void){return 0;}
const Pet *pet_at(int index){(void)index;return NULL;}

int main(void){
  memset(&W,0,sizeof W);W.in_room=true;W.w=W.h=8;W.owner=W.guest_id=9;W.door_x=0;W.door_y=3;
  snprintf(W.model,sizeof W.model,"cuarto");memset(W.rows,'0',64);
  W.items[0]=(Item){.used=true,.id=300,.owner=9,.def=6,.x=3,.y=3,.z_fine=32};
  BuildGhost ghost={.on=true,.def=6,.x=3,.y=3};
  assert(client_build_valid(&ghost));assert(ghost.z==6&&ghost.z_fine==32);assert(sizeof ghost==28);
  W.items[0].z_fine=64;assert(client_build_valid(&ghost));assert(ghost.z==6&&ghost.z_fine==64);
  ghost.x=4;assert(client_build_valid(&ghost));assert(ghost.z==0&&ghost.z_fine==0);
  W.items[0].z_fine=32;W.items[1]=(Item){.used=true,.id=301,.owner=9,.def=6,.x=3,.y=3,.z=6,.z_fine=31};
  ghost.ignore=300;assert(client_build_valid(&ghost));
  W.items[1].z_fine=32;assert(!client_build_valid(&ghost));
  ghost.ignore=0;W.ents[0]=(Ent){.used=true,.id=1,.x=4,.y=3,.fx=4,.fy=3};assert(!client_build_valid(&ghost));
  memset(W.ents,0,sizeof W.ents);ghost.x=0;assert(!client_build_valid(&ghost));
  puts("PASS portable placement: exact 6.32/6.64 previews, ground reset, fractional support protection, occupancy/door rejection and unchanged 28-byte ghost.");
  const FurniDef *stack=NULL,*walk=NULL;
  for(int i=0;i<furni_def_count;i++){
    const FurniDef *d=&furni_defs[i];
    if(d->w==1&&d->h==1&&def_height_helper(d)==1)stack=d;
    if(d->w==1&&d->h==1&&def_height_helper(d)==2)walk=d;
  }
  assert(stack&&walk);memset(W.items,0,sizeof W.items);W.rows[3*W.w+4]='x';
  W.ents[0]=(Ent){.used=true,.id=1,.x=4,.y=3,.fx=4,.fy=3};
  ghost=(BuildGhost){.on=true,.def=stack->id,.x=4,.y=3};
  assert(client_build_valid(&ghost)&&ghost.z==0&&ghost.z_fine==0);
  W.items[0]=(Item){.used=true,.id=300,.owner=9,.def=stack->id,.x=4,.y=3,.z_fine=32};
  ghost.def=6;assert(client_build_valid(&ghost)&&ghost.z==0&&ghost.z_fine==32);
  W.items[0].def=walk->id;W.items[0].z=1;W.items[0].z_fine=28;
  assert(world_walkable_tile(4,3));assert(client_build_valid(&ghost)&&ghost.z==1&&ghost.z_fine==28);
  W.items[1]=(Item){.used=true,.id=301,.owner=9,.def=4,.x=4,.y=3,.z=100};
  ghost=(BuildGhost){.on=true,.def=walk->id,.x=5,.y=3,.ignore=300};
  assert(client_build_valid(&ghost)&&ghost.z==0&&ghost.z_fine==0);
  ghost.x=0;assert(!client_build_valid(&ghost));
  puts("PASS portable helpers: occupied void placement, exact support previews, walking over void and moving an occupied helper without lifting its stack.");
}
