#include "wired_chests.h"
#include "world.h"
#include "catalog.h"
#include <jansson.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

World W;
const char *tw_translate(const char *source){return source?source:"";}
static json_t *sent;
static unsigned requests;
static u64 clock_ms=10000;
static const FurniDef defs[]={{.id=4,.name="Crystal vase"},{.id=6,.name="Wood table"}};
const FurniDef *def_get(u16 id){for(size_t i=0;i<sizeof defs/sizeof defs[0];i++)if(defs[i].id==id)return &defs[i];return NULL;}
u64 now_ms(void){return clock_ms;}
bool net_online(void){return true;}
void send_furniture(const char *text){json_decref(sent);sent=json_loads(text,0,NULL);assert(sent);requests++;}
void net_goto(const char *room,const char *ticket){(void)room;(void)ticket;}
static void receive(json_t *message){char *text=json_dumps(message,JSON_COMPACT);assert(text);assert(wired_chests_receive(text));free(text);json_decref(message);}
static json_t *chest(void){return json_pack("{s:s,s:s,s:i,s:b,s:b,s:{s:s,s:i,s:i,s:i,s:i,s:{s:b,s:b,s:b,s:b,s:b},s:{s:b,s:b,s:b,s:b,s:b,s:s,s:s,s:i,s:i,s:i,s:i,s:i},s:[{s:i,s:i}]}}","op","open","kind","wired_chest","item",20,"owned",1,"editable",1,"data","kind","furni","coins",0,"level",0,"maximumCapacity",1000,"total",1,"permissions","settings",1,"deposit",1,"withdraw",1,"upgrade",1,"unlock",1,"settings","locked",0,"autoLock",1,"wiredEnabled",1,"everyoneCanOpen",0,"everyoneCanDonate",0,"name","Store","description","","capacity",1000,"stateMode",0,"previewMode",0,"previewAmount",1,"notifyMode",0,"contents","id",31,"def",4);}
static json_t *transaction(bool confirmed){json_t *packet=json_pack("{s:s,s:s,s:{s:b,s:[i],s:{s:i,s:i,s:[[{s:i,s:n}]],s:[]}}}","op","open","kind","wired_transaction","data","confirmed",confirmed,"offered",31,"contract","kind",1,"paymentMode",1,"payment","amount",1,"definition","reward");json_object_set_new(json_object_get(packet,"data"),"source",json_integer(30));return packet;}
int main(void){
    W.inv_n=1;W.inv[0]=(InvItem){31,4};
    receive(chest());assert(wired_chest_panel.kind==WC_CHEST);wired_chests_action(WC_WITHDRAW);wired_chests_select(0);unsigned before=requests;wired_chests_action(WC_DONE);assert(wired_chest_panel.view==WC_REVIEW);assert(requests==before);wired_chests_action(WC_EXECUTE);assert(!strcmp(json_string_value(json_object_get(sent,"op")),"chest_withdraw"));assert(json_integer_value(json_array_get(json_object_get(sent,"items"),0))==31);assert(json_is_string(json_object_get(sent,"requestKey")));
    wired_chests_close();assert(!strcmp(json_string_value(json_object_get(sent,"op")),"chest_close"));
    receive(transaction(false));before=requests;assert(requests==before);wired_chests_action(WC_ACCEPT);assert(!json_is_true(json_object_get(sent,"final")));before=requests;receive(transaction(true));assert(requests==before);assert(wired_chest_panel.view==WC_MAIN);wired_chests_action(WC_ACCEPT);assert(json_is_true(json_object_get(sent,"final")));wired_chests_close();assert(!strcmp(json_string_value(json_object_get(sent,"op")),"transaction_cancel"));
    receive(json_pack("{s:s,s:s,s:i,s:b,s:{s:{s:i,s:i,s:[],s:[],s:s,s:i,s:b,s:s,s:s}}}","op","open","kind","wired_contract","item",21,"editable",1,"data","contract","kind",1,"paymentMode",1,"payment","reward","layout","generic","rewardCategory",11,"showDialog",0,"rewardText","","receiveText",""));
    wired_chests_action(WC_PAYMENTS);wired_chests_action(WC_ADD_NODE);assert(wired_chests_edit(0,"2"));wired_chests_action(WC_SAVE_NODE);wired_chests_action(WC_BACK);wired_chests_action(WC_REWARDS);wired_chests_action(WC_ADD_NODE);wired_chests_action(WC_NODE_TYPE);assert(wired_chest_panel.rows==1);wired_chests_select(0);wired_chests_action(WC_SAVE_NODE);wired_chests_action(WC_SAVE_CONTRACT);json_t *contract=json_object_get(sent,"contract");assert(json_integer_value(json_object_get(contract,"kind"))==1);assert(json_integer_value(json_object_get(json_array_get(json_array_get(json_object_get(contract,"payment"),0),0),"amount"))==2);assert(json_integer_value(json_object_get(json_array_get(json_object_get(contract,"reward"),0),"definition"))==4);wired_chests_reset();
    receive(json_pack("{s:s,s:s,s:i,s:b,s:{s:s,s:i,s:i,s:b}}","op","open","kind","wired_label","item",22,"editable",1,"data","text","Read this label","color",4278912,"visibility",1,"alwaysOpen",1));wired_chests_action(WC_CONFIGURE);wired_chests_select(0);assert(wired_chests_field_limit(0)==1000);char long_text[1001];memset(long_text,'A',1000);long_text[1000]=0;assert(wired_chests_edit(0,long_text));wired_chests_action(WC_DONE);wired_chests_action(WC_SAVE_SETTINGS);assert(strlen(json_string_value(json_object_get(json_object_get(sent,"label"),"text")))==1000);assert(!strcmp(json_string_value(json_object_get(sent,"op")),"wired_label_save"));wired_chests_reset();
    receive(json_pack("{s:s,s:i,s:i,s:s,s:b,s:[{s:s,s:s,s:i,s:b}]}","type","wired_link","item",23,"result",0,"network","0","receiveLocal",0,"networks","id","7","name","Room network","connections",2,"linked",0));wired_chests_action(WC_SHOW_TERMS);wired_chests_select(0);wired_chests_action(WC_LINK_DELETE);before=requests;assert(wired_chest_panel.view==WC_REVIEW);assert(requests==before);wired_chests_action(WC_EXECUTE);assert(json_integer_value(json_object_get(sent,"operation"))==4);assert(!strcmp(json_string_value(json_object_get(sent,"network")),"7"));wired_chests_reset();
    receive(chest());unsigned previous=requests;receive(json_pack("{s:s,s:i,s:b,s:i}","op","transaction_result","source",30,"success",0,"reason",3));assert(wired_chest_panel.kind==WC_CHEST&&wired_chest_panel.view==WC_MAIN);assert(requests==previous);receive(chest());assert(requests==previous);receive(transaction(false));assert(requests==previous+1);assert(!strcmp(json_string_value(json_object_get(sent,"op")),"chest_close"));previous=requests;receive(transaction(true));assert(requests==previous);receive(chest());assert(requests==previous+1);assert(!strcmp(json_string_value(json_object_get(sent,"op")),"transaction_cancel"));receive(json_pack("{s:s,s:i,s:b,s:i}","op","transaction_result","source",30,"success",0,"reason",3));assert(wired_chest_panel.kind==WC_CHEST&&wired_chest_panel.view==WC_MAIN);wired_chests_reset();
    receive(chest());wired_chests_action(WC_UPGRADE);assert(wired_chests_edit(0,"2"));wired_chests_action(WC_REVIEW_ACTION);assert(strstr(wired_chest_panel.body,"40 credits"));wired_chests_action(WC_EXECUTE);assert(json_integer_value(json_object_get(sent,"count"))==2);wired_chests_reset();json_decref(sent);puts("Wired chest controls: explicit transfers, two-phase consent, contract types, long labels, network deletion and upgrade prices passed.");return 0;
}
