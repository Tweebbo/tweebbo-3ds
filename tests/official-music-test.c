#include "official_music.h"
#include <assert.h>
#include <limits.h>

static void row(json_t *rows,json_int_t item,const char *code,const char *title){
  json_array_append_new(rows,json_pack("{s:I,s:s,s:s}","item",item,"code",code,"title",title));
}
int main(void){
  assert(official_music_sample("official","double_peks")==1073);
  assert(!official_music_sample("classic","double_peks"));
  assert(!official_music_sample("official","Double_Peks"));
  assert(!official_music_sample("official","../classic/1"));
  assert(!official_music_sample("official","unknown"));
  MusicDiscGroup groups[MUSIC_DISC_GROUPS]={0};json_t *rows=json_array();
  row(rows,9002,"double_peks","Habbowood");row(rows,9001,"double_peks","Habbowood");
  row(rows,-1,"invalid","Negative");row(rows,0,"zero","Zero");row(rows,(json_int_t)UINT32_MAX+1,"overflow","Overflow");
  row(rows,UINT32_MAX,"edge","Maximum item identity");
  json_array_append_new(rows,json_pack("{s:s,s:s,s:s}","item","9003","code","typed","title","Wrong numeric type"));
  assert(official_music_groups(rows,groups)==2);
  assert(groups[0].item==9002&&groups[0].count==2&&!strcmp(groups[0].code,"double_peks")&&!strcmp(groups[0].title,"Habbowood"));
  assert(groups[1].item==UINT32_MAX&&groups[1].count==1);
  json_decref(rows);rows=json_array();
  for(int i=0;i<25;i++){char code[32];snprintf(code,sizeof code,"code_%d",i);row(rows,i+1,code,"Original source title");}
  assert(official_music_groups(rows,groups)==MUSIC_DISC_GROUPS);
  for(int i=0;i<MUSIC_DISC_GROUPS;i++)assert(groups[i].item==(uint32_t)i+1&&groups[i].count==1);
  json_decref(rows);rows=json_array();char oversized[66];memset(oversized,'a',65);oversized[65]=0;
  row(rows,1,oversized,"Too long");assert(!official_music_groups(rows,groups));
  json_decref(rows);assert(!official_music_groups(NULL,groups));
  puts("Official music grouping, exact owned IDs, capacity and media whitelist passed.");return 0;
}
