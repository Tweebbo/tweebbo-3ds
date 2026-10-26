#include "language.h"
#include <3ds.h>
#include "ui_int.h"
#include "fastfood.h"
#include "game_audio.h"
#include "world.h"
#include "net.h"
#include <stdio.h>
#include <string.h>
static json_t *state(void){return json_object_get(fastfood_snapshot(),"state");}
static const char *str(json_t *j,const char *key){const char *s=json_string_value(json_object_get(j,key));return s?s:"";}
static int num(json_t *j,const char *key){return json_integer_value(json_object_get(j,key));}
static int page;
void food_open(void){ui_open(P_FASTFOOD);Ent *me=world_ent(W.me);fastfood_open(W.name,me?me->look:0);}
static void drop(void){json_t *p=json_array_get(json_object_get(state(),"players"),num(fastfood_snapshot(),"slot"));fastfood_action(json_is_object(json_object_get(p,"plate"))?"parachute":"drop");}
void food_input(u32 down){
  if(down&KEY_B){ui_close();return;}
  if(!state()||json_is_null(state()))return;
  if(down&KEY_A)drop();
  if(down&KEY_X)fastfood_action("big");
  if(down&KEY_Y)fastfood_action("missile");
  if(down&KEY_R)fastfood_action("shield");
}
void food_draw(u64 now){
  (void)now;if(sk_window(WIN_X,WIN_Y,WIN_W,WIN_H,TR("Fast Food"))){ui_close();return;}sk_content(IN_X,IN_Y,IN_W,IN_H);
  if(fastfood_error()[0]){sk_paragraph(fastfood_error(),18,49,284,12,SK_TEXT,6);return;}
  json_t *s=state(),*root=fastfood_snapshot(),*lobby=json_object_get(root,"lobby");
  if(json_is_object(s)){
    json_t *p=json_array_get(json_object_get(s,"players"),num(root,"slot"));char line[100];
    snprintf(line,sizeof line,"%s: %d / %d",str(p,"name"),num(p,"score"),num(s,"goal"));sk_text(line,18,46,12,SK_TEXT,0,284);
    bool playing=!strcmp(str(s,"phase"),"playing");
    if(sk_button(18,74,284,json_is_object(json_object_get(p,"plate"))?TR("Open parachute (A)"):TR("Drop (A)"),playing))drop();
    if(sk_button(18,104,138,TR("Big chute (X)"),playing))fastfood_action("big");
    if(sk_button(164,104,138,TR("Missile (Y)"),playing))fastfood_action("missile");
    if(sk_button(18,134,138,TR("Shield (R)"),playing))fastfood_action("shield");
    if(sk_button(164,134,138,TR("Back to games"),true))fastfood_menu();
    sk_text(TR(!strcmp(str(s,"phase"),"playing")?"Playing":!strcmp(str(s,"phase"),"countdown")?"Get ready":"Match finished"),18,173,11,SK_DIM,0,138);
    if(sk_button(164,170,138,game_audio_muted()?TR("Unmute"):TR("Mute"),true))game_audio_toggle();
    return;
  }
  if(json_is_object(lobby)){
    json_t *games=json_object_get(lobby,"games");int current=num(lobby,"current");
    if(current){sk_text(TR("Waiting for players"),18,48,12,SK_TEXT,0,284);if(sk_button(18,80,138,TR("Start"),true))fastfood_command("{\"op\":\"start\"}");if(sk_button(164,80,138,TR("Leave game"),true))fastfood_command("{\"op\":\"leave\"}");}
    else{
      int pages=((int)json_array_size(games)+1)/2;if(page>=pages||page<0)page=0;
      for(int i=0;i<2;i++){json_t *g=json_array_get(games,page*2+i);if(!g)break;char label[80],command[64];snprintf(label,sizeof label,TR("Game %d | %u players"),num(g,"id"),(unsigned)json_array_size(json_object_get(g,"players")));snprintf(command,sizeof command,"{\"op\":\"join\",\"game\":%d}",num(g,"id"));if(sk_button(18,45+i*26,284,label,!json_is_true(json_object_get(g,"running"))))fastfood_command(command);}
      if(pages>1){if(sk_button(18,99,138,TR("Previous"),page>0))page--;if(sk_button(164,99,138,TR("Next"),page+1<pages))page++;}
      if(!json_array_size(games))sk_text(TR("No games yet. Create one below."),18,50,11,SK_DIM,0,284);
      for(int i=0;i<3;i++){char label[20],command[40];snprintf(label,sizeof label,TR("Create %d"),6+i*6);snprintf(command,sizeof command,"{\"op\":\"create\",\"goal\":%d}",6+i*6);if(sk_button(18+i*96,128,92,label,true))fastfood_command(command);}
    }
    if(sk_button(18,174,138,TR("Refresh"),true))fastfood_command("{\"op\":\"list\"}");
    if(sk_button(164,174,138,TR("Practice"),true))fastfood_practice(12);
    sk_text(str(root,"error"),18,154,10,SK_DIM,0,284);return;
  }
  if(json_is_true(json_object_get(root,"online"))){
    sk_paragraph(str(root,"error")[0]?str(root,"error"):TR("Connecting to Fast Food..."),18,48,284,12,SK_TEXT,5);
    if(sk_button(18,128,284,TR("Retry"),net_online()))fastfood_online();
    if(sk_button(18,174,284,TR("Practice"),true))fastfood_practice(12);
    return;
  }
  sk_paragraph(TR("Land your dishes gently. Choose a menu to practise against computer players."),18,48,284,12,SK_TEXT,3);
  for(int i=0;i<3;i++){char label[24];snprintf(label,sizeof label,TR("%d dishes"),6+i*6);if(sk_button(18+i*96,104,92,label,true))fastfood_practice(6+i*6);}
  if(sk_button(18,145,284,TR("Play online"),net_online()))fastfood_online();
}
