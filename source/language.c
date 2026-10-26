#include "language.h"
#include "net.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *codes[]={"en","es","zh","ja","fr","pt-BR"};
static const char *names[]={"English","Español","中文","日本語","Français","Português (Brasil)"};
static int current, pending=-1;
static unsigned char *dictionary;
static size_t dictionary_size;
static uint32_t count;
static const char *error="";
static uint32_t read32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int tw_language(void){return current;}
const char *tw_language_code(void){return codes[current];}
const char *tw_language_name(int language){return names[language>=0&&language<TW_LANGUAGE_COUNT?language:0];}
const char *tw_language_error(void){return error;}
const char *tw_reception_language(void){return current==TW_FR||current==TW_PT_BR?"en":codes[current];}
const char *tw_reception_slug(void){static const char *slugs[]={"lobby","lobby-es","lobby-zh","lobby-ja","lobby","lobby"};return slugs[current];}
void tw_language_request(int language){if(language>=0&&language<TW_LANGUAGE_COUNT)pending=language;}
void tw_language_init(void){char value[16];if(store_load("language",value,sizeof value))for(int i=0;i<TW_LANGUAGE_COUNT;i++)if(!strcmp(value,codes[i]))pending=i;tw_language_update();}

static bool valid(const unsigned char *data,size_t size,uint32_t *entries){
  if(size<8||memcmp(data,"TWL1",4))return false;
  uint32_t n=read32(data+4);
  if(n>20000||8+(size_t)n*8>size)return false;
  const char *previous=NULL;
  for(uint32_t i=0;i<n;i++){
    uint32_t key=read32(data+8+i*8),value=read32(data+12+i*8);
    if(key<8+n*8||key>=size||value<8+n*8||value>=size||!memchr(data+key,0,size-key)||!memchr(data+value,0,size-value))return false;
    const char *source=(const char *)data+key;
    if(previous&&strcmp(previous,source)>=0)return false;
    previous=source;
  }
  *entries=n;return true;
}
void tw_language_update(void){
  if(pending<0)return;
  int next=pending;pending=-1;error="";
  if(next==current)return;
  unsigned char *data=NULL;size_t size=0;uint32_t entries=0;
  if(next!=TW_EN){
    char path[64];
#if TWEEBBO_DEVICE == 3
    snprintf(path,sizeof path,"app0:lang/%s.bin",codes[next]);
#else
    snprintf(path,sizeof path,"romfs:/lang/%s.bin",codes[next]);
#endif
    FILE *file=fopen(path,"rb");
    if(!file)goto failed;
    if(fseek(file,0,SEEK_END)){fclose(file);goto failed;}
    long length=ftell(file);
    if(length<8||length>2*1024*1024||fseek(file,0,SEEK_SET)){fclose(file);goto failed;}
    size=(size_t)length;data=malloc(size);
    bool complete=data&&fread(data,1,size,file)==size;fclose(file);
    if(!complete||!valid(data,size,&entries))goto failed;
  }
  if(!gfx_language_font(next))goto failed;
  free(dictionary);dictionary=data;dictionary_size=size;count=entries;current=next;
  store_save("language",codes[current]);
  return;
failed:
  free(data);error="Could not load the language. Please try again.";
}
const char *tw_translate(const char *source){
  if(!source)return "";
  if(!dictionary||!dictionary_size)return source;
  int lo=0,hi=(int)count-1;
  while(lo<=hi){int mid=(lo+hi)/2;const unsigned char *entry=dictionary+8+mid*8;int cmp=strcmp(source,(const char *)dictionary+read32(entry));if(!cmp)return (const char *)dictionary+read32(entry+4);if(cmp>0)lo=mid+1;else hi=mid-1;}
  return source;
}
const FontGlyph *tw_cjk_glyph(unsigned code){
  const FontGlyph *glyphs=current==TW_JA?font_ja_glyphs:font_zh_glyphs;
  int lo=0,hi=(current==TW_JA?font_ja_count:font_zh_count)-1;
  while(lo<=hi){int mid=(lo+hi)/2;unsigned key=glyphs[mid].code;if(key==code)return glyphs+mid;if(key<code)lo=mid+1;else hi=mid-1;}return NULL;
}
