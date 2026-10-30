#include "../source/world.h"
#include "../source/lookup_cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
int main(void){
 memset(&W,0,sizeof W);
 for(unsigned i=0;i<MAX_ENTS;i++){W.ents[i].used=true;W.ents[i].id=4000000000u+i*31;}
 for(unsigned round=0;round<10000;round++){
  unsigned index=round%MAX_ENTS,id=W.ents[index].id;assert(world_ent(id)==&W.ents[index]);
  W.ents[index].used=false;assert(!world_ent(id));W.ents[index].used=true;W.ents[index].id=10000+round;assert(!world_ent(id));assert(world_ent(W.ents[index].id)==&W.ents[index]);
 }
 unsigned old=W.ents[2].id;W.ents[90]=W.ents[2];W.ents[2].used=false;assert(world_ent(old)==&W.ents[90]);
 memset(W.ents,0,sizeof W.ents);assert(!world_ent(old));
 LookupCache cache={0};uint32_t keys[160];
 for(unsigned i=0;i<160;i++)keys[i]=(1000+i)*256+(i%4);
 for(unsigned pass=0;pass<1000;pass++){
  for(unsigned i=0;i<160;i++)lookup_cache_put(&cache,keys[i],i);
  for(unsigned i=0;i<160;i++){int slot=lookup_cache_get(&cache,keys[i]);assert(slot==-1||(unsigned)slot==i);}
  keys[pass%160]+=256000;
 }
 for(unsigned i=0;i<MAX_ENTS;i++){W.ents[i].used=true;W.ents[i].id=100000+i*31;}
 volatile unsigned sum=0;clock_t start=clock();
 for(unsigned n=0;n<4000000;n++){unsigned id=W.ents[n%MAX_ENTS].id;for(unsigned i=0;i<MAX_ENTS;i++)if(W.ents[i].used&&W.ents[i].id==id){sum+=i;break;}}
 double linear=1000.0*(clock()-start)/CLOCKS_PER_SEC;start=clock();
 for(unsigned n=0;n<4000000;n++)sum+=(unsigned)(world_ent(W.ents[n%MAX_ENTS].id)-W.ents);
 double cached=1000.0*(clock()-start)/CLOCKS_PER_SEC;
 printf("PASS128-player lookup,10000replacements/removals,slot relocation,room reset,resident IDs and furniture-key collisions; host-only4m queries %.2fms linear -> %.2fms cached; checksum=%u memory=%zu bytes/cache\n",linear,cached,sum,sizeof cache);
}
