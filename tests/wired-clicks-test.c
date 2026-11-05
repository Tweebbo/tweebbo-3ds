#include "wired_clicks.h"
#include <jansson.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool builder;
static unsigned requests;
static json_t *last;
bool world_can_decorate(void){return builder;}
bool net_online(void){return true;}
void send_furniture(const char *text){json_decref(last);last=json_loads(text,0,NULL);assert(last);requests++;}
int main(void){
    wired_clicks_reset();assert(!wired_clicks_active());wired_clicks_user(7);assert(requests==1);assert(json_integer_value(json_object_get(last,"id"))==7);assert(wired_clicks_receive("{\"op\":\"wired_click_result\",\"user\":7,\"openMenu\":true}"));assert(wired_clicks_menu_user()==7);assert(!wired_clicks_menu_user());
    assert(wired_clicks_receive("{\"op\":\"click_settings\",\"users\":2,\"furniture\":1}"));assert(wired_clicks_user_mode()==2);assert(wired_clicks_furniture_through());wired_clicks_user(7);wired_clicks_furniture(8);assert(requests==1);wired_clicks_toggle();assert(wired_clicks_user_mode()==2);
    builder=true;wired_clicks_toggle();assert(wired_clicks_ignored());assert(!wired_clicks_user_mode());assert(!wired_clicks_furniture_through());wired_clicks_user(9);assert(requests==2);builder=false;assert(wired_clicks_user_mode()==2);assert(!wired_clicks_ignored());
    assert(wired_clicks_receive("{\"op\":\"click_settings\",\"users\":1,\"furniture\":0}"));wired_clicks_user(10);assert(requests==3);wired_clicks_receive("{\"op\":\"wired_click_result\",\"user\":10,\"openMenu\":true}");assert(!wired_clicks_menu_user());wired_clicks_furniture(11);assert(requests==4);assert(!strcmp(json_string_value(json_object_get(last,"kind")),"furniture"));wired_clicks_receive("{\"op\":\"click_settings\",\"users\":99,\"furniture\":0}");assert(wired_clicks_user_mode()==1);wired_clicks_reset();assert(!wired_clicks_active());assert(!wired_clicks_user_mode());json_decref(last);puts("Wired click controls: normal, walk-behind, pass-through, pending reply, builder override and room reset passed.");return 0;
}
