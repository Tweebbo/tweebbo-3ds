#include "wired_services.h"
#include "world.h"
#include "net.h"
#include "language.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WiredServicePanel wired_service_panel;
static json_t *data;
static int offer_index,confirm_action,editing;
static json_int_t selected_id;
static u64 sent_at,purchase_look;
static const char *string(json_t *object,const char *key){const char *value=json_string_value(json_object_get(object,key));return value?value:"";}
static json_int_t number(json_t *object,const char *key){return json_integer_value(json_object_get(object,key));}
static bool flag(json_t *object,const char *key){return json_is_true(json_object_get(object,key));}
static void copy(char *destination,size_t capacity,const char *text){if(!capacity)return;size_t size=strlen(text?text:"");if(size>=capacity){size=capacity-1;while(size&&((unsigned char)text[size]&0xc0)==0x80)size--;}memcpy(destination,text?text:"",size);destination[size]=0;}
static json_t *rows(void){return json_object_get(data,wired_service_panel.kind==WS_BOTS?"bots":wired_service_panel.kind==WS_GROUPS?"groups":"quests");}
static json_t *selected(void){json_t *array=rows();size_t index;json_t *row;json_array_foreach(array,index,row)if(number(row,"id")==selected_id)return row;return NULL;}
static json_t *offer(void){json_t *array=json_object_get(data,"offers");size_t count=json_array_size(array);if(!count)return NULL;if(offer_index<0)offer_index=(int)count-1;offer_index%=(int)count;return json_array_get(array,(size_t)offer_index);}
static bool send(json_t *request){char *text=json_dumps(request,JSON_COMPACT);json_decref(request);if(!text)return false;if(!net_online()){copy(wired_service_panel.status,sizeof wired_service_panel.status,"Connect to the hotel to use this service.");free(text);return false;}send_furniture(text);free(text);wired_service_panel.pending=true;sent_at=now_ms();copy(wired_service_panel.status,sizeof wired_service_panel.status,"Waiting for the hotel...");wired_service_panel.revision++;return true;}
static void request_list(void){send(json_pack("{s:s}","op",wired_service_panel.kind==WS_BOTS?"bot_inventory":wired_service_panel.kind==WS_GROUPS?"groups_list":"quest_list"));}
static void button(int action,const char *label,bool enabled){WiredServicePanel *panel=&wired_service_panel;if(panel->button_count>=WS_BUTTONS)return;WiredServiceButton *button=&panel->buttons[panel->button_count++];button->action=action;button->enabled=enabled&&!panel->pending;copy(button->label,sizeof button->label,TR(label));}
static void describe(void){
    WiredServicePanel *panel=&wired_service_panel;panel->button_count=0;panel->field_count=0;panel->rows=(int)json_array_size(rows());
    copy(panel->title,sizeof panel->title,panel->kind==WS_BOTS?"Room bots":panel->kind==WS_GROUPS?"Groups":"Quests");panel->body[0]=0;
    if(panel->view==WS_LIST){
        copy(panel->body,sizeof panel->body,panel->rows?"Choose an entry to see its details.":panel->pending?"Loading...":"No entries are available.");
        if(panel->kind==WS_BOTS)button(WS_NEW,"Buy a bot",json_array_size(json_object_get(data,"offers"))>0);
        if(panel->kind==WS_GROUPS){button(WS_NEW,"Create group",flag(data,"canCreate"));if(!flag(data,"clubActive"))copy(panel->body,sizeof panel->body,"Creating a group requires Club, 150 credits and an owned room without a group.");}
        button(WS_REFRESH,"Refresh",true);return;
    }
    if(panel->view==WS_FORM){
        panel->field_count=panel->kind==WS_GROUPS?2:1;
        if(editing==WS_RENAME){copy(panel->title,sizeof panel->title,"Rename bot");copy(panel->body,sizeof panel->body,"Choose a name with 1 to 32 characters.");button(WS_REVIEW,"Review name",true);return;}
        if(panel->kind==WS_BOTS){json_t *preset=offer();snprintf(panel->body,sizeof panel->body,TR("%s costs %lld credits. The bot will use your current wardrobe outfit."),string(preset,"name"),(long long)number(preset,"price"));button(WS_PREV,"Previous preset",true);button(WS_NEXT,"Next preset",true);button(WS_REVIEW,"Review purchase",true);}
        else{snprintf(panel->body,sizeof panel->body,TR("Creating this group costs %lld credits and links it to this room."),(long long)number(data,"price"));button(WS_REVIEW,"Review purchase",flag(data,"canCreate"));}return;
    }
    if(panel->view==WS_CONFIRM){copy(panel->title,sizeof panel->title,"Confirm");button(WS_CONFIRM_ACTION,"Confirm",true);return;}
    json_t *row=selected();if(!row){panel->view=WS_LIST;describe();return;}
    copy(panel->title,sizeof panel->title,string(row,"name"));
    if(panel->view==WS_POSITION){snprintf(panel->body,sizeof panel->body,TR("Choose a free tile for %s. Current tile: %d, %d."),string(row,"name"),panel->tile_x,panel->tile_y);button(WS_NORTH,"North",panel->tile_y>0);button(WS_EAST,"East",panel->tile_x+1<W.w);button(WS_SOUTH,"South",panel->tile_y+1<W.h);button(WS_WEST,"West",panel->tile_x>0);button(WS_REVIEW,"Review position",world_tile(panel->tile_x,panel->tile_y));return;}
    if(panel->kind==WS_BOTS){const char *room=string(row,"room");bool inventory=!room[0],here=!strcmp(room,W.slug);snprintf(panel->body,sizeof panel->body,"%s\n%s",TR(number(row,"kind")==1?"Source preset: Skye (bartender type).":"Source preset: Robbie (generic type)."),TR(inventory?"In your inventory.":here?"Placed in this room.":"Placed in another room."));
        if(inventory){button(WS_PLACE,"Place in room",world_can_decorate());}
        if(here){button(WS_PICKUP,"Pick up",true);button(WS_OUTFIT,"Use current outfit",true);}
        if(inventory||here){button(WS_RENAME,"Rename",true);}
        if(inventory){button(WS_DELETE,"Delete bot",true);}
        if(!inventory&&!here){button(WS_VISIT,"Visit room",true);}
    }else if(panel->kind==WS_GROUPS){snprintf(panel->body,sizeof panel->body,TR("%s\nOwner: %s%s"),string(row,"description"),string(row,"ownerName"),flag(row,"equipped")?TR("\nYou are wearing this group's badge."):"");json_t *permissions=json_object_get(row,"permissions");if(flag(permissions,"join"))button(WS_JOIN,"Join group",true);if(flag(permissions,"equip"))button(WS_EQUIP,flag(row,"equipped")?"Remove badge":"Wear badge",true);if(flag(permissions,"leave"))button(WS_LEAVE,"Leave group",true);if(flag(permissions,"linkRoom"))button(WS_LINK_ROOM,"Link this room",flag(data,"canLinkRoom"));if(string(row,"homeRoom")[0])button(WS_VISIT,"Visit home room",true);
    }else{snprintf(panel->body,sizeof panel->body,TR("Progress: %lld of %lld\nReward: %lld duckets\nCompleted steps: %lld of %lld%s"),(long long)number(row,"progress"),(long long)number(row,"target"),(long long)number(row,"reward"),(long long)number(row,"completedSteps"),(long long)number(row,"totalSteps"),flag(row,"available")?"":TR("\nThis goal is not available in this client."));button(flag(row,"active")?WS_QUEST_CANCEL:WS_QUEST_START,flag(row,"active")?"Cancel quest":"Start quest",flag(row,"active")||flag(row,"available"));}
}
void wired_services_reset(void){json_decref(data);data=NULL;memset(&wired_service_panel,0,sizeof wired_service_panel);offer_index=confirm_action=editing=0;selected_id=0;sent_at=purchase_look=0;}
void wired_services_close(void){wired_services_reset();}
void wired_services_open(int kind){wired_services_reset();WiredServicePanel *panel=&wired_service_panel;panel->open=true;panel->kind=kind;panel->selected=-1;describe();request_list();}
void wired_services_update(void){if(wired_service_panel.pending&&now_ms()-sent_at>15000){wired_service_panel.pending=false;copy(wired_service_panel.status,sizeof wired_service_panel.status,"No reply. Use Refresh to check the current state.");describe();wired_service_panel.revision++;}}
bool wired_services_receive(const char *text){json_error_t error;json_t *object=json_loads(text,0,&error);if(!object)return false;const char *op=string(object,"op");int kind=!strcmp(op,"bots")?WS_BOTS:!strcmp(op,"groups")?WS_GROUPS:!strcmp(op,"quests")?WS_QUESTS:0;if(!kind){json_decref(object);return false;}WiredServicePanel *panel=&wired_service_panel;if(panel->open&&panel->kind==kind){json_decref(data);data=object;panel->pending=false;panel->status[0]=0;if(panel->view!=WS_LIST)panel->view=selected()?WS_DETAIL:WS_LIST;describe();panel->revision++;}else json_decref(object);return true;}
void wired_services_select(int index){if(wired_service_panel.pending)return;json_t *row=json_array_get(rows(),(size_t)index);if(!row)return;selected_id=number(row,"id");wired_service_panel.selected=index;wired_service_panel.view=WS_DETAIL;describe();wired_service_panel.revision++;}
static void confirm(int action,const char *text){confirm_action=action;wired_service_panel.view=WS_CONFIRM;describe();copy(wired_service_panel.body,sizeof wired_service_panel.body,text);wired_service_panel.revision++;}
static void execute(int action){
    WiredServicePanel *panel=&wired_service_panel;json_t *row=selected();const char *op=NULL;json_t *request=NULL;
    if(action==WS_NEW){if(panel->kind==WS_BOTS){json_t *preset=offer();request=json_pack("{s:s,s:I,s:s,s:I}","op","bot_buy","offer",number(preset,"id"),"name",panel->name,"look",(json_int_t)purchase_look);}else request=json_pack("{s:s,s:s,s:s}","op","group_create","name",panel->name,"description",panel->description);}
    else if(action==WS_PLACE)request=json_pack("{s:s,s:I,s:i,s:i}","op","bot_place","bot",selected_id,"x",panel->tile_x,"y",panel->tile_y);
    else if(action==WS_RENAME)request=json_pack("{s:s,s:I,s:s}","op","bot_rename","bot",selected_id,"name",panel->name);
    else if(action==WS_OUTFIT)request=json_pack("{s:s,s:I,s:I}","op","bot_outfit","bot",selected_id,"look",(json_int_t)purchase_look);
    else{switch(action){case WS_PICKUP:op="bot_pickup";break;case WS_DELETE:op="bot_delete";break;case WS_JOIN:op="group_join";break;case WS_LEAVE:op="group_leave";break;case WS_EQUIP:op="group_equip";break;case WS_LINK_ROOM:op="group_link_room";break;case WS_QUEST_START:op="quest_start";break;case WS_QUEST_CANCEL:op="quest_cancel";break;default:return;}json_int_t id=action==WS_EQUIP&&flag(row,"equipped")?0:selected_id;request=json_pack("{s:s,s:I}","op",op,panel->kind==WS_BOTS?"bot":panel->kind==WS_GROUPS?"group":"quest",id);}
    if(request)send(request);
}
void wired_services_action(int action){
    WiredServicePanel *panel=&wired_service_panel;if(panel->pending)return;json_t *row=selected();char message[512];
    if(action==WS_BACK){if(panel->view==WS_CONFIRM)panel->view=confirm_action==WS_NEW||confirm_action==WS_RENAME?WS_FORM:confirm_action==WS_PLACE?WS_POSITION:WS_DETAIL;else panel->view=WS_LIST;describe();panel->revision++;return;}
    if(action==WS_REFRESH){request_list();describe();return;}
    if(action==WS_CONFIRM_ACTION){if(panel->view==WS_CONFIRM)execute(confirm_action);return;}
    if(action==WS_NEW){editing=0;panel->view=WS_FORM;panel->description[0]=0;copy(panel->name,sizeof panel->name,panel->kind==WS_BOTS?string(offer(),"name"):"");describe();panel->revision++;return;}
    if(action==WS_RENAME){editing=WS_RENAME;panel->view=WS_FORM;copy(panel->name,sizeof panel->name,string(row,"name"));describe();panel->revision++;return;}
    if(action==WS_PREV||action==WS_NEXT){offer_index+=action==WS_NEXT?1:-1;copy(panel->name,sizeof panel->name,string(offer(),"name"));describe();panel->revision++;return;}
    if(action==WS_PLACE){Ent *me=world_ent(W.me);panel->tile_x=me?me->x:W.door_x;panel->tile_y=me?me->y:W.door_y;panel->view=WS_POSITION;describe();panel->revision++;return;}
    if(action>=WS_NORTH&&action<=WS_WEST){int dx=action==WS_EAST?1:action==WS_WEST?-1:0,dy=action==WS_SOUTH?1:action==WS_NORTH?-1:0;panel->tile_x+=dx;panel->tile_y+=dy;if(panel->tile_x<0)panel->tile_x=0;if(panel->tile_y<0)panel->tile_y=0;if(panel->tile_x>=W.w)panel->tile_x=W.w-1;if(panel->tile_y>=W.h)panel->tile_y=W.h-1;describe();panel->revision++;return;}
    if(action==WS_REVIEW){Ent *me=world_ent(W.me);purchase_look=me?me->look:0;
        if(panel->view==WS_POSITION){snprintf(message,sizeof message,TR("Place %s on tile %d, %d?"),string(row,"name"),panel->tile_x,panel->tile_y);confirm(WS_PLACE,message);return;}
        if(!panel->name[0]){copy(panel->status,sizeof panel->status,"Enter a name first.");return;}
        if(editing==WS_RENAME){snprintf(message,sizeof message,TR("Rename this bot to %s?"),panel->name);confirm(WS_RENAME,message);return;}
        if(panel->kind==WS_BOTS)snprintf(message,sizeof message,TR("Buy %s for %lld credits using your current outfit?"),panel->name,(long long)number(offer(),"price"));else snprintf(message,sizeof message,TR("Create %s for %lld credits and link it to this room?"),panel->name,(long long)number(data,"price"));confirm(WS_NEW,message);return;
    }
    if(action==WS_VISIT){const char *room=string(row,panel->kind==WS_BOTS?"room":"homeRoom");if(room[0])net_goto(room,"");return;}
    if(action==WS_OUTFIT){Ent *me=world_ent(W.me);purchase_look=me?me->look:0;snprintf(message,sizeof message,TR("Give %s your current wardrobe outfit?"),string(row,"name"));confirm(action,message);return;}
    if(action==WS_DELETE||action==WS_LEAVE||action==WS_LINK_ROOM||action==WS_QUEST_CANCEL){snprintf(message,sizeof message,TR(action==WS_DELETE?"Permanently delete %s?":action==WS_LEAVE?"Leave %s?":action==WS_LINK_ROOM?"Link %s to this room?":"Cancel %s?"),string(row,"name"));confirm(action,message);return;}
    execute(action);describe();
}
bool wired_services_edit(int field,const char *text){int length=0;for(const unsigned char *p=(const unsigned char *)text;*p;p++)if((*p&0xc0)!=0x80)length++;if(length>wired_services_field_limit(field))return false;if(field==0)copy(wired_service_panel.name,sizeof wired_service_panel.name,text);else if(field==1)copy(wired_service_panel.description,sizeof wired_service_panel.description,text);else return false;wired_service_panel.revision++;return true;}
int wired_services_field_limit(int field){return field==1?255:wired_service_panel.kind==WS_GROUPS?50:32;}
const char *wired_services_field_label(int field){return TR(field==1?"Description":wired_service_panel.kind==WS_GROUPS?"Group name":"Bot name");}
const char *wired_services_field_value(int field){return field==1?wired_service_panel.description:wired_service_panel.name;}
void wired_services_row(int index,char *title,size_t title_size,char *detail,size_t detail_size){json_t *row=json_array_get(rows(),(size_t)index);copy(title,title_size,string(row,"name"));if(wired_service_panel.kind==WS_BOTS)copy(detail,detail_size,TR(!string(row,"room")[0]?"In inventory":!strcmp(string(row,"room"),W.slug)?"In this room":"In another room"));else if(wired_service_panel.kind==WS_GROUPS)snprintf(detail,detail_size,"%s%s",TR(flag(row,"member")?"Member":"Public group"),flag(row,"equipped")?TR(", wearing badge"):"");else snprintf(detail,detail_size,"%lld / %lld%s",(long long)number(row,"progress"),(long long)number(row,"target"),flag(row,"active")?TR(", active"):"");}
u64 wired_services_preview_look(void){Ent *me=world_ent(W.me);return me?me->look:0;}

void wired_services_quest_progress(long long quest,long long progress){WiredServicePanel *p=&wired_service_panel;if(!p->open||p->kind!=WS_QUESTS||quest<1||progress<0)return;size_t index;json_t *row;json_array_foreach(rows(),index,row)if(number(row,"id")==quest){json_object_set_new(row,"progress",json_integer(progress));describe();p->revision++;break;}}
