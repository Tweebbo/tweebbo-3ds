#include "wired_visuals.h"
#include "wired_visual_source.h"
#include "world.h"
#include <jansson.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

World W;
extern size_t wired_visuals_test_status_visits;
static int rectangles,texts;static bool exact_number,observed_timed;
u64 now_ms(void){return 1000;}
Ent *world_ent(u32 id){for(int i=0;i<MAX_ENTS;i++)if(W.ents[i].used&&W.ents[i].id==id)return &W.ents[i];return NULL;}
Item *world_item(u32 id){for(int i=0;i<MAX_ITEMS;i++)if(W.items[i].used&&W.items[i].id==id)return &W.items[i];return NULL;}
void fill_rect(float x,float y,float w,float h,u32 color){(void)x;(void)y;(void)color;assert(w>=0&&h>=0);rectangles++;}
float text_width(const char *s,float size){return (float)strlen(s)*size*.5f;}
float text_draw(const char *s,float x,float y,float size,u32 color,int align){(void)x;(void)y;(void)color;(void)align;texts++;if(!strcmp(s,"778"))observed_timed=true;if(strstr(s,"9223372036854775807"))exact_number=true;return text_width(s,size);}
static void receive(json_t *value){char *raw=json_dumps(value,JSON_COMPACT);assert(raw);assert(wired_visuals_receive(raw));free(raw);json_decref(value);}
int main(void){
  W.items[0]=(Item){.used=true,.id=1,.def=5,.owner=7};W.ents[0]=(Ent){.used=true,.id=10,.guest=7};
  assert(wired_visuals_bytes()<=131072);wired_visuals_reset();
  assert(wired_visuals_receive("{\"type\":\"wired_visibility\",\"users\":false,\"ids\":[1],\"visibility\":100,\"clickThrough\":true}"));assert(wired_visuals_alpha(false,1,1000)==0&&wired_visuals_click_through(false,1));
  assert(wired_visuals_receive("{\"type\":\"wired_visibility\",\"users\":false,\"ids\":[1],\"visibility\":100,\"clickThrough\":false}"));assert(wired_visuals_alpha(false,1,1000)==0&&!wired_visuals_click_through(false,1));
  assert(wired_visuals_receive("{\"type\":\"wired_visibility\",\"users\":false,\"ids\":[1],\"visibility\":0,\"clickThrough\":false}"));
  assert(wired_visuals_receive("{\"type\":\"wired_highlight\",\"users\":false,\"ids\":[1],\"kind\":2,\"strength\":50,\"color\":0,\"animation\":0}"));assert(wired_visuals_tint(false,1,1000)==0xffacacac);
  assert(wired_visual_quantize(52)==50&&wired_visual_quantize(53)==55&&wired_visual_quantize(.01f)==5);
  assert(wired_visual_strength(100,0,0,true,0)==0&&wired_visual_strength(100,0,250,true,0)==50&&wired_visual_strength(100,0,500,true,0)==100&&wired_visual_strength(100,0,1000,true,0)==0);
  assert(wired_visuals_receive("{\"type\":\"wired_visibility\",\"users\":false,\"ids\":[1],\"transparency\":100,\"easing\":2,\"clickThrough\":false}"));assert(wired_visuals_alpha(false,1,1000)==255&&wired_visuals_alpha(false,1,1250)==191&&wired_visuals_alpha(false,1,1500)==0&&!wired_visuals_click_through(false,1));
  assert(wired_visuals_receive("{\"type\":\"wired_visibility\",\"users\":false,\"ids\":[1],\"transparency\":0,\"initialize\":true,\"easing\":2}"));assert(wired_visuals_alpha(false,1,1000)==255);
  json_t *configs=json_array(),*statuses=json_array();int renderers[]={10,2,20,3,100,201};
  for(int i=0;i<6;i++){json_t *c=json_pack("{s:i,s:b,s:i,s:i,s:i,s:i,s:s,s:s,s:{s:s,s:s}}","id",100+i,"user",1,"category",i,"renderer",renderers[i],"width",2,"color",i==5?5:1,"min","0","max","100","extra","icon",i==3?"energy":"","stack_order","0");json_array_append_new(configs,c);char key[32];snprintf(key,sizeof key,"%d:0:10",100+i);json_array_append_new(statuses,json_pack("{s:s,s:i,s:b,s:i,s:s,s:[i,b],s:{s:s,s:s}}","key",key,"config",100+i,"user",1,"entity",10,"value",i==5?"9223372036854775807":"50","identity",7,0,"extra","current_level","2","max_level","10"));}
  receive(json_pack("{s:s,s:b,s:o,s:o}","type","wired_variable_fx","reset",1,"configs",configs,"statuses",statuses));
  wired_visuals_draw(true,10,150,200,1,false,1000);assert(rectangles>6&&texts>=5&&exact_number);
  receive(json_pack("{s:s,s:[{s:i,s:b,s:i,s:i,s:i,s:i,s:s,s:s,s:i,s:i,s:i,s:b}],s:[{s:s,s:i,s:b,s:i,s:s,s:[i,b],s:b}]}","type","wired_variable_fx","configs","id",100,"user",1,"category",5,"renderer",200,"width",2,"color",1,"min","0","max","100","showMode",1,"updateMask",2,"duration",1500,"hover",1,"statuses","key","100:0:10","config",100,"user",1,"entity",10,"value","777","identity",7,0,"initialize",1));
  receive(json_pack("{s:s,s:[{s:s,s:i,s:b,s:i,s:s,s:[i,b],s:b}]}","type","wired_variable_fx","statuses","key","100:0:10","config",100,"user",1,"entity",10,"value","778","identity",7,0,"initialize",0));
  observed_timed=false;wired_visuals_draw(true,10,150,200,1,false,2499);assert(observed_timed);observed_timed=false;wired_visuals_draw(true,10,150,200,1,false,2500);assert(!observed_timed);wired_visuals_draw(true,10,150,200,1,true,2500);assert(observed_timed);
  W.ents[0].guest=8;texts=0;wired_visuals_draw(true,10,150,200,1,false,1000);assert(texts==0);W.ents[0].guest=7;
  wired_visuals_forget(true,10);texts=0;wired_visuals_draw(true,10,150,200,1,false,1000);assert(texts==0);
  receive(json_pack("{s:s,s:b,s:[{s:i,s:s,s:i,s:b}]}","type","wired_labels","replace",1,"labels","item",1,"text","A public label with a readable hover preview","color",4278912,"alwaysOpen",0));
  texts=0;wired_visuals_draw(false,1,150,200,1,false,1000);assert(texts==0);wired_visuals_draw(false,1,150,200,1,true,1000);assert(texts>0);
  wired_visuals_forget(false,1);assert(wired_visuals_alpha(false,1,1000)==255&&!wired_visuals_click_through(false,1));texts=0;wired_visuals_draw(false,1,150,200,1,true,1000);assert(texts==0);
  wired_visuals_reset();configs=json_array();statuses=json_array();
  for(int target=0;target<2;target++)for(int c=0;c<2;c++)json_array_append_new(configs,json_pack("{s:i,s:b,s:i,s:i,s:i,s:i,s:s,s:s,s:{s:s}}","id",200+target*2+c,"user",target,"category",1,"renderer",12,"width",0,"color",1,"min","0","max","100","extra","segments","100"));
  for(int i=0;i<MAX_ITEMS;i++)W.items[i]=(Item){.used=true,.id=1000+i,.def=5,.owner=7};
  for(int i=0;i<MAX_ENTS;i++)W.ents[i]=(Ent){.used=true,.id=10000+i,.guest=7};
  for(int i=0;i<1000;i++){int holder=i%(MAX_ITEMS+MAX_ENTS),user=holder>=MAX_ITEMS,entity=user?10000+holder-MAX_ITEMS:1000+holder,config=200+user*2+i/(MAX_ITEMS+MAX_ENTS);char key[48];snprintf(key,sizeof key,"%d:%d:%d",config,user?0:1,entity);json_array_append_new(statuses,json_pack("{s:s,s:i,s:b,s:i,s:s,s:[i,i]}","key",key,"config",config,"user",user,"entity",entity,"value","50","identity",user?7:5,user?0:7));}
  receive(json_pack("{s:s,s:b,s:o,s:o}","type","wired_variable_fx","reset",1,"configs",configs,"statuses",statuses));
  wired_visuals_test_status_visits=0;
  for(int i=0;i<MAX_ITEMS;i++)wired_visuals_draw(false,1000+i,150,200,1,false,1000);
  for(int i=0;i<MAX_ENTS;i++)wired_visuals_draw(true,10000+i,150,200,1,false,1000);
  assert(wired_visuals_test_status_visits==1000);
  assert(!wired_visuals_receive("{\"op\":\"other-service\"}"));
  printf("PASS: native Wired visibility, click-through, highlight, all six value displays, exact signed64 text, identity guards, detach and filtered labels; 1000 indexed status visits across640 holders; bounded state %zu bytes.\n",wired_visuals_bytes());
}
