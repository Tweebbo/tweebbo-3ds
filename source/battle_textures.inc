/* Included by each backend after its checked texture drawing function. */
static BATTLE_TEXTURE battle_pages[4];
static unsigned battle_loaded;
static int battle_arena;
void gfx_battle_free(void){
  while(battle_loaded){--battle_loaded;BATTLE_FREE(battle_pages[battle_loaded]);}
  battle_arena=0;
}
bool gfx_battle_load(int arena){
#ifdef BATTLE_AVAILABLE
  if(!BATTLE_AVAILABLE)return false;
#endif
  if(arena<1||arena>5)return false;
  if(battle_arena==arena&&battle_loaded==battle_page_count[arena-1])return true;
  gfx_battle_free();if(battle_page_count[arena-1]>COUNT(battle_pages))return false;
  for(unsigned i=0;i<battle_page_count[arena-1];i++){
    char path[128];snprintf(path,sizeof path,BATTLE_DIRECTORY"/%s"BATTLE_EXTENSION,battle_page_files[arena-1][i]);
    if(!BATTLE_LOAD(path,&battle_pages[i])){gfx_battle_free();return false;}battle_loaded++;
  }
  battle_arena=arena;return true;
}
void gfx_battle_sprite(unsigned id,float x,float y,float kx,float ky,u32 color,float left,float top,float right,float bottom){
  if(id>=battle_sprite_count||!kx||ky<=0)return;
  const BattleSprite *s=&battle_sprites[id];if(s->page>=battle_loaded)return;
  bool flip=kx<0;float scale=fabsf(kx);x+=flip?-s->w*scale:0;
  int l=(int)fmaxf(0,ceilf((left-x)/scale)),t=(int)fmaxf(0,ceilf((top-y)/ky));
  int r=(int)fminf(s->w,floorf((right-x)/scale)),b=(int)fminf(s->h,floorf((bottom-y)/ky));
  if(r<=l||b<=t)return;
  int sx=s->x+(flip?s->w-r:l);
  BATTLE_DRAW(battle_pages[s->page],sx,s->y+t,r-l,b-t,x+l*scale,y+t*ky,scale,ky,flip,color);
}
#undef BATTLE_TEXTURE
#undef BATTLE_FREE
#undef BATTLE_LOAD
#undef BATTLE_DRAW
#undef BATTLE_DIRECTORY
#undef BATTLE_EXTENSION
#undef BATTLE_AVAILABLE
