#include "avatar_effects.h"
#include "gfx.h"
#include "gen/igloo_fx_pixels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

const AvatarFxAnimation *avatar_effect(u16 id){
  unsigned lo=0,hi=avatar_fx_animation_count;
  while(lo<hi){unsigned mid=(lo+hi)/2;if(avatar_fx_animations[mid].id<id)lo=mid+1;else hi=mid;}
  return lo<avatar_fx_animation_count&&avatar_fx_animations[lo].id==id?&avatar_fx_animations[lo]:NULL;
}
const AvatarFxFrame *avatar_effect_frame(u16 id,int direction,u64 tick,bool moving){
  const AvatarFxAnimation *effect=avatar_effect(id);if(!effect)return NULL;
  unsigned period=moving?effect->moving_period:effect->period;
  if(!period)return NULL;
  const u16 *frames=moving?effect->moving:effect->still;
  return &avatar_fx_frames[frames[(direction&7)*period+tick%period]];
}
static u32 white(u8 alpha){return 0x00ffffffu|((u32)alpha<<24);}
static void draw_layer(const AvatarFxLayer *layer,float x,float y,float scale){
  if(layer->image==65535)return;
  const AvatarFxImage *image=&avatar_fx_images[layer->image];
  if(layer->ink==33)gfx_additive(true);
  gfx_sprite(image->page,image->x,image->y,image->w,image->h,x-layer->x*scale,y-layer->y*scale,scale,false,white(layer->alpha));
  if(layer->ink==33)gfx_additive(false);
}
void avatar_effect_layers(u16 id,int direction,u64 tick,bool moving,float x,float y,float scale,bool behind){
  const AvatarFxFrame *frame=avatar_effect_frame(id,direction,tick,moving);if(!frame)return;
  for(unsigned i=0;i<frame->count;i++){
    const AvatarFxLayer *layer=&avatar_fx_layers[frame->first+i];
    if((layer->depth<0)==behind)draw_layer(layer,x,y,scale);
  }
}
bool avatar_effect_shadow(u16 id,int direction,float x,float y,float scale){
  const AvatarFxAnimation *effect=avatar_effect(id);if(!effect)return false;
  const AvatarFxLayer *shadow=&effect->shadow[direction&7];if(shadow->image==65535)return false;
  AvatarFxLayer layer=*shadow;layer.alpha=(u8)((unsigned)layer.alpha*64/255);
  draw_layer(&layer,x,y,scale);return true;
}

typedef struct {u32 offset,size;u16 width,height;} PixelEntry;
static FILE *pixels_file;
static PixelEntry *pixels_index;
static unsigned pixels_count;
static long pixels_length;
static bool pixels_attempted;
static u32 little32(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static u16 little16(const u8 *p){return (u16)(p[0]|(p[1]<<8));}
static const PeopleSprite *pixel_sprite(unsigned index){
  if(index<(unsigned)people_sprite_count)return &people_sprites[index];
  index-=(unsigned)people_sprite_count;
  if(index<(unsigned)swim_sprite_count)return &swim_sprites[index];
  index-=(unsigned)swim_sprite_count;
  return index<PENGUIN_PIXEL_COUNT?&penguin_pixel_sprites[index]:NULL;
}
bool avatar_effect_penguin_part(unsigned frame,bool body,u32 color,AvatarFxDraw *part){
  if(!part||frame>=PENGUIN_PIXEL_COUNT/2)return false;
  unsigned index=frame*2+(body?1:0);
  const PeopleSprite *sprite=&penguin_pixel_sprites[index];
  *part=(AvatarFxDraw){.sprite=(unsigned)(people_sprite_count+swim_sprite_count)+index,.x=-sprite->rx,.y=-sprite->ry,.color=color};
  return true;
}
void avatar_effect_pixels_close(void){
  if(pixels_file)fclose(pixels_file);
  pixels_file=NULL;free(pixels_index);pixels_index=NULL;pixels_count=0;pixels_attempted=false;
}
static bool open_pixels(void){
  if(pixels_file)return true;
  if(pixels_attempted)return false;
  pixels_attempted=true;
#if defined(__vita__) || (TWEEBBO_DEVICE==3 && !defined(TWEEBBO_HOST))
  const char *path="app0:fxpeople.pak";
#else
  const char *path="romfs:/fxpeople.pak";
#endif
  pixels_file=fopen(path,"rb");if(!pixels_file)return false;
  u8 header[8];
  if(fread(header,1,8,pixels_file)!=8||memcmp(header,"FXP1",4))goto invalid;
  pixels_count=little32(header+4);
  if(pixels_count!=(unsigned)(people_sprite_count+swim_sprite_count+PENGUIN_PIXEL_COUNT)||pixels_count>65536)goto invalid;
  if(fseek(pixels_file,0,SEEK_END))goto invalid;
  pixels_length=ftell(pixels_file);
  if(pixels_length<8+(long)pixels_count*12||fseek(pixels_file,8,SEEK_SET))goto invalid;
  pixels_index=calloc(pixels_count,sizeof *pixels_index);if(!pixels_index)goto invalid;
  for(unsigned i=0;i<pixels_count;i++){
    u8 raw[12];if(fread(raw,1,12,pixels_file)!=12)goto invalid;
    PixelEntry *entry=&pixels_index[i];entry->offset=little32(raw);entry->size=little32(raw+4);entry->width=little16(raw+8);entry->height=little16(raw+10);
    const PeopleSprite *sprite=pixel_sprite(i);
    if(!sprite||!entry->width||!entry->height||entry->width>512||entry->height>512||entry->width!=sprite->w||entry->height!=sprite->h
       ||entry->offset<8+pixels_count*12||(u64)entry->offset+entry->size>(u64)pixels_length||entry->size>(u32)entry->width*entry->height*5)goto invalid;
  }
  return true;
invalid:
  avatar_effect_pixels_close();pixels_attempted=true;dbg_log("Avatar effect pixel source is invalid.");return false;
}
static u8 byte(double value){long n=lrint(value);return n<0?0:n>255?255:(u8)n;}
static u8 *read_pixels(unsigned sprite){
  if(!open_pixels()||sprite>=pixels_count)return NULL;
  PixelEntry *entry=&pixels_index[sprite];size_t count=(size_t)entry->width*entry->height,bytes=count*4;
  u8 *encoded=malloc(entry->size),*pixels=malloc(bytes);
  if(!encoded||!pixels){free(encoded);free(pixels);return NULL;}
  if(fseek(pixels_file,entry->offset,SEEK_SET)||fread(encoded,1,entry->size,pixels_file)!=entry->size)goto invalid;
  size_t at=0,out=0;
  while(at<entry->size&&out<count){
    u8 control=encoded[at++];size_t run=(control&127)+1;
    if(run>count-out)goto invalid;
    if(control&128){if(entry->size-at<4)goto invalid;for(size_t i=0;i<run;i++)memcpy(pixels+(out+i)*4,encoded+at,4);at+=4;}
    else{if(entry->size-at<run*4)goto invalid;memcpy(pixels+out*4,encoded+at,run*4);at+=run*4;}
    out+=run;
  }
  if(at!=entry->size||out!=count)goto invalid;
  free(encoded);
  return pixels;
invalid:
  free(encoded);free(pixels);return NULL;
}
bool avatar_effect_compose(const AvatarFxDraw *parts,unsigned count,bool mirrored,u16 effect_id,AvatarFxBitmap *bitmap){
  memset(bitmap,0,sizeof *bitmap);
  const AvatarFxAnimation *effect=avatar_effect(effect_id);
  if((effect_id&&(!effect||!effect->transform))||!count||count>64)return false;
  int left=32767,top=32767,right=-32768,bottom=-32768;
  for(unsigned i=0;i<count;i++){
    const PeopleSprite *sprite=pixel_sprite(parts[i].sprite);int factor=parts[i].scale?parts[i].scale:1;
    if(!sprite||factor>2)return false;
    if(parts[i].x<left)left=parts[i].x;
    if(parts[i].y<top)top=parts[i].y;
    if(parts[i].x+sprite->w*factor>right)right=parts[i].x+sprite->w*factor;
    if(parts[i].y+sprite->h*factor>bottom)bottom=parts[i].y+sprite->h*factor;
  }
  int width=right-left,height=bottom-top;
  if(width<=0||height<=0||width>512||height>512)return false;
  size_t pixels_count=(size_t)width*height;
  u8 *pixels=calloc(pixels_count,4);if(!pixels)return false;
  for(unsigned i=0;i<count;i++){
    const AvatarFxDraw *part=&parts[i];const PeopleSprite *sprite=pixel_sprite(part->sprite);unsigned factor=part->scale?part->scale:1;
    u8 *source=read_pixels(part->sprite);if(!source){free(pixels);return false;}
    unsigned red=part->color>>16&255,green=part->color>>8&255,blue=part->color&255;
    const unsigned tint[3]={red,green,blue},shift[3]={16,8,0};unsigned overlay_alpha=part->overlay>>24;
    for(unsigned pixel=0;pixel<(unsigned)sprite->w*sprite->h;pixel++){
      u8 *s=source+pixel*4;
      for(unsigned channel=0;channel<3;channel++){
        u8 colored=byte(s[channel]*tint[channel]/255.);unsigned overlay=part->overlay>>shift[channel]&255;
        s[channel]=s[3]?byte((colored*(255-overlay_alpha)+overlay*overlay_alpha)/255.):0;
      }
    }
    for(unsigned y=0;y<sprite->h;y++)for(unsigned x=0;x<sprite->w;x++){
      const u8 *s=source+((size_t)y*sprite->w+x)*4,*quadrants[4]={s,s,s,s};
      if(factor==2){
        const u8 *b=source+((size_t)(y?y-1:y)*sprite->w+x)*4;
        const u8 *d=source+((size_t)y*sprite->w+(x?x-1:x))*4;
        const u8 *f=source+((size_t)y*sprite->w+(x+1<sprite->w?x+1:x))*4;
        const u8 *h=source+((size_t)(y+1<sprite->h?y+1:y)*sprite->w+x)*4;
        if(memcmp(b,h,4)&&memcmp(d,f,4)){
          if(!memcmp(d,b,4))quadrants[0]=d;if(!memcmp(b,f,4))quadrants[1]=f;
          if(!memcmp(d,h,4))quadrants[2]=d;if(!memcmp(h,f,4))quadrants[3]=f;
        }
      }
      unsigned tx=part->x-left+(mirrored?sprite->w-1-x:x)*factor,ty=part->y-top+y*factor;
      for(unsigned oy=0;oy<factor;oy++)for(unsigned ox=0;ox<factor;ox++){
        const u8 *s=quadrants[oy*2+ox];if(!s[3])continue;
        u8 *d=pixels+(((size_t)ty+oy)*width+tx+(mirrored?factor-1-ox:ox))*4;
        double a=s[3]/255.,previous=d[3]/255.,combined=a+previous*(1-a);
        for(unsigned channel=0;channel<3;channel++){
          d[channel]=byte((s[channel]*a+d[channel]*previous*(1-a))/combined);
        }
        d[3]=byte(combined*255);
      }
    }
    free(source);
  }
  for(size_t i=0;effect&&effect->transform&&i<pixels_count;i++){
    u8 *pixel=pixels+i*4;
    double level=(pixel[0]*.2126+pixel[1]*.7152+pixel[2]*.0722)/255.;
    const unsigned shift[4]={16,8,0,24};
    for(unsigned channel=0;channel<4;channel++){
      double background=effect->background>>shift[channel]&255,foreground=effect->foreground>>shift[channel]&255;
      double value=background+(foreground-background)*level;
      pixel[channel]=byte(channel==3?pixel[3]*value/255.:value);
    }
  }
  bitmap->pixels=pixels;bitmap->x=left;bitmap->y=top;bitmap->width=width;bitmap->height=height;return true;
}
