#include "doorbell.h"
#include "net.h"
#include "world.h"
#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
World W;
static char identity[80]="fixture-owner", room[64]="fixture-room",path[160],body[512];
static int id,ready,status=200,resumed;
static char *reply;
static u64 clock_ms=100;
u64 now_ms(void){return clock_ms;}
const char *net_token(void){return identity;}
const char *net_room(void){return room;}
bool net_online(void){return true;}
void net_resume(void){resumed++;}
int net_http(const char *method,const char *url,const char *json,bool auth){assert(auth);snprintf(path,sizeof path,"%s",url);snprintf(body,sizeof body,"%s",json?json:"");return ++id;}
bool net_http_result(int job,HttpResult *out){if(!ready)return false;assert(job==id);memset(out,0,sizeof *out);out->status=status;out->body=reply;ready=0;return true;}
static void respond(int code,const char *text){status=code;reply=strdup(text);ready=1;doorbell_poll();}
int main(void){
  doorbell_poll();doorbell_ring();assert(strstr(path,"/api/rooms/doorbell"));assert(strstr(body,"fixture-room"));
  respond(200,"{\"status\":\"waiting\"}");assert(doorbell.waiting);clock_ms+=3100;doorbell_poll();
  respond(200,"{\"status\":\"approved\"}");assert(resumed==1&&!doorbell.waiting);
  doorbell_ring();snprintf(identity,sizeof identity,"another-fixture");
  respond(200,"{\"status\":\"approved\"}");assert(resumed==1);
  W.room_owner=W.in_room=true;snprintf(W.slug,sizeof W.slug,"%s",room);clock_ms+=3100;doorbell_poll();
  respond(200,"{\"owner\":true,\"doorbell\":true,\"requests\":[{\"guest\":42,\"name\":\"Visitor\"}]}");assert(doorbell.enabled&&doorbell.guest==42&&doorbell.count==1);
  doorbell_answer(true);assert(!strcmp(path,"/api/rooms/doorbell/answer"));json_t *j=json_loads(body,0,NULL);assert(json_integer_value(json_object_get(j,"guest"))==42);assert(json_is_true(json_object_get(j,"allow")));json_decref(j);
  respond(403,"{\"error\":\"Owner changed\"}");assert(strstr(doorbell.status,"Owner changed"));assert(!doorbell.busy);
  W.room_owner=false;doorbell_answer(true);assert(!doorbell.busy);
  W.room_owner=true;doorbell_set(false);assert(!strcmp(path,"/api/rooms/lock"));respond(200,"{\"doorbell\":false}");assert(!doorbell.enabled);
  puts("Native doorbell: async approval, identity isolation, owner queue, refusal and access settings passed.");return 0;
}
