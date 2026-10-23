#include "doorbell.h"
#include "net.h"
#include "world.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
DoorbellState doorbell;
static int job, operation;
static u64 next_poll;
static char room[64], identity[128], job_room[64], job_identity[128];
static void message(const char *s){snprintf(doorbell.status,sizeof doorbell.status,"%s",s);}
static void request(const char *method,const char *path,json_t *value,int op){
  if(job){json_decref(value);return;}
  char *body=value?json_dumps(value,JSON_COMPACT):NULL;json_decref(value);
  job=net_http(method,path,body,true);free(body);operation=op;doorbell.busy=job!=0;
  snprintf(job_room,sizeof job_room,"%s",net_room());snprintf(job_identity,sizeof job_identity,"%s",net_token());
  if(!job)message("Could not reach the doorbell. Try again.");
}
void doorbell_ring(void){
  if(!*net_token()){message("Enter the lobby to create your character first.");return;}
  if(doorbell.waiting||job)return;
  message("Ringing...");request("POST","/api/rooms/doorbell",json_pack("{s:s}","slug",net_room()),1);
}
void doorbell_answer(bool allow){
  if(!W.room_owner||!doorbell.guest||job)return;
  request("POST","/api/rooms/doorbell/answer",json_pack("{s:s,s:I,s:b}","slug",net_room(),"guest",(json_int_t)doorbell.guest,"allow",allow),2);
}
void doorbell_set(bool enabled){
  if(!W.room_owner||!net_online()||job)return;
  request("POST","/api/rooms/lock",json_pack("{s:s,s:s,s:b}","slug",net_room(),"password","","doorbell",enabled),3);
}
void doorbell_poll(void){
  if(strcmp(room,net_room())||strcmp(identity,net_token())){
    memset(&doorbell,0,sizeof doorbell);next_poll=0;
    snprintf(room,sizeof room,"%s",net_room());snprintf(identity,sizeof identity,"%s",net_token());
  }
  HttpResult result;
  if(job&&net_http_result(job,&result)){
    job=0;doorbell.busy=false;
    if(strcmp(job_room,net_room())||strcmp(job_identity,net_token())){free(result.body);return;}
    json_t *data=result.body?json_loads(result.body,0,NULL):NULL;
    if(result.status!=200){const char *error=json_string_value(json_object_get(data,"error"));message(error?error:"Could not check the doorbell. Try again.");doorbell.waiting=false;}
    else if(operation==3){doorbell.enabled=json_is_true(json_object_get(data,"doorbell"));message(doorbell.enabled?"Doorbell enabled. You approve each visitor.":"Room open to visitors.");next_poll=now_ms()+3000;}
    else if(operation==2){doorbell.guest=0;doorbell.count=0;next_poll=0;message("Answer saved.");}
    else if(json_is_true(json_object_get(data,"owner"))){
      doorbell.enabled=json_is_true(json_object_get(data,"doorbell"));json_t *queue=json_object_get(data,"requests"),*first=json_array_get(queue,0);
      doorbell.count=(int)json_array_size(queue);doorbell.guest=(u32)json_integer_value(json_object_get(first,"guest"));
      const char *name=json_string_value(json_object_get(first,"name"));snprintf(doorbell.name,sizeof doorbell.name,"%s",name?name:"");
      message(doorbell.count?"Someone is waiting at the door.":"Nobody is waiting at the door.");
    }else{
      const char *state=json_string_value(json_object_get(data,"status"));if(!state)state="expired";
      doorbell.waiting=!strcmp(state,"waiting");
      if(!strcmp(state,"approved")||!strcmp(state,"open")){doorbell.waiting=false;net_resume();}
      else message(doorbell.waiting?"Waiting for the room owner...":!strcmp(state,"denied")?"The owner declined your request.":!strcmp(state,"offline")?"The owner is not here to answer.":"No answer. You can ring again.");
    }
    json_decref(data);free(result.body);
  }
  bool owner=W.room_owner&&W.in_room&&net_online()&&!strcmp(W.slug,net_room());
  if(!job&&*net_token()&&(owner||doorbell.waiting)&&now_ms()>=next_poll){
    char path[128];snprintf(path,sizeof path,"/api/rooms/doorbell?slug=%s",net_room());next_poll=now_ms()+3000;request("GET",path,NULL,0);
  }
}
