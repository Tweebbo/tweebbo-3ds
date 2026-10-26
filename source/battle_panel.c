#include "language.h"
#include "battle_panel.h"
#include "game_audio.h"
#include "world.h"
#include "net.h"
#include <stdio.h>
#include <string.h>
static int arena=5,teams=2,minutes=3,page;
static const char *arenas[]={"","Sky Peak","Coral Beach","Maze Park","Gothic Hallway","Barebones Classic"};
static const char *team_names[]={"Red","Blue","Yellow","Green"};
static int num(json_t *j,const char *key){return json_integer_value(json_object_get(j,key));}
static const char *str(json_t *j,const char *key){const char *s=json_string_value(json_object_get(j,key));return s?s:"";}
static json_t *mine(void){json_t *players=json_object_get(battle_snapshot(),"players");for(size_t i=0;i<json_array_size(players);i++){json_t *p=json_array_get(players,i);if((u32)num(p,"eid")==W.me)return p;}return NULL;}
void battle_panel_reset(void){arena=5;teams=2;minutes=3;page=0;}
const char *battle_power_name(void){
  static const char *names[]={"No power","Light bulb","Spring","Flashlight","Cannon","Pins","Harlequin","Bomb","Vacuum","Mystery"};
  int kind=json_integer_value(json_array_get(json_object_get(mine(),"powers"),0));return TR(names[kind>=0&&kind<=9?kind:0]);
}
void battle_score(char *out,int size){
  json_t *g=battle_snapshot(),*players=json_object_get(g,"players");int scores[4]={0};
  for(size_t i=0;i<json_array_size(players);i++){json_t *p=json_array_get(players,i);int t=num(p,"team");if(t>=0&&t<4)scores[t]+=num(p,"score");}
  if(size<=0)return;
  out[0]=0;
  int count=num(g,"teams");if(count<2)count=2;if(count>4)count=4;
  for(int i=0;i<count;i++){size_t n=strlen(out);if(n>=(size_t)size-1)break;snprintf(out+n,size-n,"%s%s %d",i?"   ":"",TR(team_names[i]),scores[i]);}
}
void battle_panel_action(int command,int argument){
  char json[128];json_t *g=battle_snapshot();
  switch(command){
    case BATTLE_ARENA:arena=arena%5+1;battle_preview(arena);break;
    case BATTLE_TEAMS:teams=teams%4+1;if(teams<2)teams=2;break;
    case BATTLE_MINUTES:minutes=minutes==2?3:minutes==3?5:2;break;
    case BATTLE_CREATE:case BATTLE_PRACTICE:snprintf(json,sizeof json,"{\"op\":\"battle_create\",\"arena\":%d,\"teams\":%d,\"minutes\":%d,\"practice\":%s}",arena,teams,minutes,command==BATTLE_PRACTICE?"true":"false");battle_command(json);break;
    case BATTLE_JOIN:battle_command("{\"op\":\"battle_join\"}");break;
    case BATTLE_TEAM:snprintf(json,sizeof json,"{\"op\":\"battle_team\",\"team\":%d}",(num(mine(),"team")+1)%((num(g,"teams")>=2&&num(g,"teams")<=4)?num(g,"teams"):2));battle_command(json);break;
    case BATTLE_START:battle_command("{\"op\":\"battle_start\"}");break;
    case BATTLE_FILL:battle_command("{\"op\":\"battle_start\",\"fill\":true}");break;
    case BATTLE_POWER:battle_command("{\"op\":\"battle_power\",\"slot\":0}");break;
    case BATTLE_ZOOM:battle_zoom(argument);break;
    case BATTLE_CENTRE:battle_centre();break;
    case BATTLE_LEAVE:battle_close();break;
    case BATTLE_PAGE:page=argument;break;
    case BATTLE_SOUND:game_audio_toggle();break;
  }
}
void battle_panel(float x,float y,float width,BattleButton button,BattleLabel label){
  char text[160];json_t *g=battle_snapshot(),*self=mine();
  if(battle_error()[0]){label(battle_error(),x,y,width);return;}
  if(!net_online()){label(TR("Connect to the hotel to play Battle Ball."),x,y,width);return;}
  if(battle_playing()){
    battle_score(text,sizeof text);label(text,x,y,width);
    char clock[32];battle_status(clock,sizeof clock);
#if TWEEBBO_DEVICE == 3
    snprintf(text,sizeof text,TR("%s | Pad: move. Cross: power. L/R: zoom."),clock);
#else
    snprintf(text,sizeof text,TR("%s | Pad: move. A: power. L/R: zoom."),clock);
#endif
    label(text,x,y+24,width);
    button(x,y+48,width,25,battle_power_name(),BATTLE_POWER,0);
    button(x,y+78,width/2-3,25,TR("Zoom +"),BATTLE_ZOOM,1);button(x+width/2+3,y+78,width/2-3,25,TR("Zoom -"),BATTLE_ZOOM,-1);
    button(x,y+108,width/3-3,25,TR("Centre"),BATTLE_CENTRE,0);button(x+width/3,y+108,width/3-3,25,game_audio_muted()?TR("Unmute"):TR("Mute"),BATTLE_SOUND,0);button(x+width*2/3,y+108,width/3,25,TR("Leave"),BATTLE_LEAVE,0);return;
  }
  if(!json_is_object(g)||num(g,"phase")==2){
    if(num(g,"phase")==2){int winner=num(g,"winner");snprintf(text,sizeof text,TR("Winner: %s"),TR(json_is_integer(json_object_get(g,"winner"))&&winner>=0&&winner<4?team_names[winner]:"Draw"));label(text,x,y,width);}
    else label(TR("Choose an arena and create a game."),x,y,width);
    button(x,y+23,width,25,TR(arenas[arena]),BATTLE_ARENA,0);
    snprintf(text,sizeof text,TR("%d teams"),teams);button(x,y+53,width/2-3,25,text,BATTLE_TEAMS,0);
    snprintf(text,sizeof text,TR("%d minutes"),minutes);button(x+width/2+3,y+53,width/2-3,25,text,BATTLE_MINUTES,0);
    button(x,y+85,width/2-3,25,TR("Create"),BATTLE_CREATE,0);button(x+width/2+3,y+85,width/2-3,25,TR("Practice"),BATTLE_PRACTICE,0);
    label(TR("Practice uses computer players in this room."),x,y+122,width);return;
  }
  json_t *players=json_object_get(g,"players");int count=json_array_size(players),pages=(count+2)/3;if(page<0||page>=pages)page=0;
  for(int i=0;i<3;i++){json_t *p=json_array_get(players,page*3+i);if(!p)break;int team=num(p,"team");snprintf(text,sizeof text,"%s: %s",TR(team_names[team>=0&&team<4?team:0]),str(p,"name"));label(text,x,y+i*19,width);}
  if(pages>1){button(x,y+57,width/2-3,23,TR("Previous"),page>0?BATTLE_PAGE:0,page-1);button(x+width/2+3,y+57,width/2-3,23,TR("Next"),page+1<pages?BATTLE_PAGE:0,page+1);}
  button(x,y+85,width/2-3,25,self?TR("Change team"):TR("Join"),self?BATTLE_TEAM:BATTLE_JOIN,0);
  button(x+width/2+3,y+85,width/2-3,25,TR("Leave"),BATTLE_LEAVE,0);
  if((u32)num(g,"host")==W.me){button(x,y+115,width/2-3,25,TR("Start"),BATTLE_START,0);button(x+width/2+3,y+115,width/2-3,25,TR("Add bots + start"),BATTLE_FILL,0);}
}
