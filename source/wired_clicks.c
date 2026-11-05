#include "wired_clicks.h"
#include "world.h"
#include "net.h"
#include <jansson.h>
#include <stdio.h>
#include <string.h>

static int user_mode,furni_mode;
static bool ignored;
static u32 pending_user,menu_user;
void wired_clicks_reset(void){user_mode=furni_mode=0;ignored=false;pending_user=menu_user=0;}
static void check_permission(void){if(ignored&&!world_can_decorate())ignored=false;}
int wired_clicks_user_mode(void){check_permission();return ignored?0:user_mode;}
bool wired_clicks_furniture_through(void){check_permission();return !ignored&&furni_mode==1;}
bool wired_clicks_active(void){return user_mode!=0||furni_mode!=0;}
bool wired_clicks_ignored(void){check_permission();return ignored;}
void wired_clicks_toggle(void){if(world_can_decorate()&&wired_clicks_active())ignored=!ignored;}
void wired_clicks_user(u32 id){if(wired_clicks_user_mode()==2||!net_online())return;char text[128];pending_user=id;snprintf(text,sizeof text,"{\"op\":\"wired_click\",\"kind\":\"user\",\"id\":%u}",(unsigned)id);send_furniture(text);}
void wired_clicks_furniture(u32 id){if(wired_clicks_furniture_through()||!net_online())return;char text[128];snprintf(text,sizeof text,"{\"op\":\"wired_click\",\"kind\":\"furniture\",\"id\":%u}",(unsigned)id);send_furniture(text);}
u32 wired_clicks_menu_user(void){u32 id=menu_user;menu_user=0;return id;}
bool wired_clicks_receive(const char *text){json_t *value=json_loads(text,0,NULL);if(!value)return false;const char *op=json_string_value(json_object_get(value,"op"));if(!op){json_decref(value);return false;}
    if(!strcmp(op,"click_settings")){json_t *u=json_object_get(value,"users"),*f=json_object_get(value,"furniture");json_int_t users=json_integer_value(u),furni=json_integer_value(f);if(json_is_integer(u)&&json_is_integer(f)&&users>=0&&users<=2&&furni>=0&&furni<=1){user_mode=(int)users;furni_mode=(int)furni;check_permission();if(!wired_clicks_active())ignored=false;}json_decref(value);return true;}
    if(!strcmp(op,"wired_click_result")){json_int_t id=json_integer_value(json_object_get(value,"user"));if(id>0&&id<=UINT32_MAX&&(u32)id==pending_user){pending_user=0;if(json_is_true(json_object_get(value,"openMenu"))&&wired_clicks_user_mode()==0)menu_user=(u32)id;}json_decref(value);return true;}
    json_decref(value);return false;
}
