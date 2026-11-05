#pragma once
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "official_music_limits.h"

enum { MUSIC_DISC_GROUPS=16 };
typedef struct {uint32_t item,count;char code[65],title[80];} MusicDiscGroup;
static inline int official_music_sample(const char *provider,const char *code){
  return !strcmp(provider,"official")&&!strcmp(code,"double_peks")?OFFICIAL_MUSIC_SAMPLE:0;
}
static inline int official_music_groups(json_t *rows,MusicDiscGroup groups[MUSIC_DISC_GROUPS]){
  int count=0;size_t index;json_t *row;
  json_array_foreach(rows,index,row){
    json_int_t item=json_integer_value(json_object_get(row,"item"));
    const char *code=json_string_value(json_object_get(row,"code")),*title=json_string_value(json_object_get(row,"title"));
    if(item<=0||(uint64_t)item>UINT32_MAX||!code||!*code||strlen(code)>64||!title)continue;
    int at=0;while(at<count&&strcmp(groups[at].code,code))at++;
    if(at==count){if(count==MUSIC_DISC_GROUPS)continue;groups[count]=(MusicDiscGroup){.item=(uint32_t)item};snprintf(groups[count].code,sizeof groups[count].code,"%s",code);snprintf(groups[count].title,sizeof groups[count].title,"%s",title);count++;}
    if(groups[at].count<UINT32_MAX)groups[at].count++;
  }
  return count;
}
