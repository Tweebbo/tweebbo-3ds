#include "wired_feedback.h"
#include "wired_services.h"
#include <jansson.h>
#include <stdio.h>
#include <string.h>

static const char *string(json_t *object,const char *key){const char *s=json_string_value(json_object_get(object,key));return s?s:"";}
static long long number(json_t *object,const char *key){return (long long)json_integer_value(json_object_get(object,key));}
bool wired_feedback_receive(const char *text,char *message,size_t capacity){
    if(!capacity)return false;
    message[0]=0;
    if(!strstr(text,"quest_progress")&&!strstr(text,"badge_received")&&!strstr(text,"wired_reward")&&!strstr(text,"wired_achievement"))return false;
    json_error_t error;json_t *value=json_loads(text,0,&error);if(!json_is_object(value)){json_decref(value);return false;}
    const char *op=string(value,"op"),*type=string(value,"type");bool handled=true;
    if(!strcmp(op,"quest_progress")){
        bool completed=json_is_true(json_object_get(value,"completed"));wired_services_quest_progress(number(value,"quest"),number(value,"progress"));
        if(completed)snprintf(message,capacity,"Quest completed.");else snprintf(message,capacity,"Quest progress: %lld.",number(value,"progress"));
    }else if(!strcmp(op,"badge_received")){snprintf(message,capacity,"Badge received.");}
    else if(!strcmp(op,"wired_reward")){
        const char *status;switch(number(value,"reason")){case 1:status="This reward has reached its limit.";break;case 2:status="This reward is not available to you.";break;case 3:status="No reward was selected.";break;case 4:status="Badge received.";break;case 5:status="Furniture reward received.";break;case 8:status="This reward is not available in this room.";break;default:status="The reward could not be given.";break;}snprintf(message,capacity,"%s",status);
    }else if(!strcmp(type,"wired_achievement")){
        if(json_array_size(json_object_get(value,"unlocked")))snprintf(message,capacity,"Achievement level unlocked: %lld.",number(value,"level"));else snprintf(message,capacity,"Achievement progress: %lld.",number(value,"progress"));
    }else if(!strcmp(type,"wired_reward_track")){snprintf(message,capacity,"Task progress: %lld. Track points: %lld.",number(value,"count"),number(value,"points"));}
    else handled=false;
    json_decref(value);return handled;
}
