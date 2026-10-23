#include "feedback.h"
#include "net.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FeedbackDraft feedback;
static int job;
static unsigned sequence;
static char identity[128], job_identity[128], request_id[33];
static char *last_payload;
static void status(const char *s) { snprintf(feedback.status,sizeof feedback.status,"%s",s); }
static int chars(const char *s) { int n=0;for(;*s;s++)if(((unsigned char)*s&0xc0)!=0x80)n++;return n; }
static void trim(char *s) {
  size_t n=strlen(s),start=0;
  while(start<n && (unsigned char)s[start]<=32)start++;
  while(n>start && (unsigned char)s[n-1]<=32)n--;
  memmove(s,s+start,n-start);s[n-start]=0;
}
bool feedback_receptionist(u32 guest,const char *model) {
  return guest==4001000000u && model && (!strcmp(model,"newbie_lobby") || !strcmp(model,"newbie_lobby_es") || !strcmp(model,"newbie_lobby_zh") || !strcmp(model,"newbie_lobby_ja"));
}
void feedback_open(void) {
  if(strcmp(identity,net_token())) {
    memset(&feedback,0,sizeof feedback);
    snprintf(identity,sizeof identity,"%s",net_token());
    free(last_payload);last_payload=NULL;request_id[0]=0;
  }
  feedback.busy=job!=0;
}
void feedback_poll(void) {
  if(strcmp(identity,net_token()))feedback_open();
  HttpResult r;
  if(!job || !net_http_result(job,&r))return;
  job=0;feedback.busy=false;
  if(strcmp(job_identity,net_token())){free(r.body);memset(job_identity,0,sizeof job_identity);return;}
  json_t *j=r.body?json_loads(r.body,0,NULL):NULL;
  const char *id=json_string_value(json_object_get(j,"id"));
  if(r.status==200 && id && *id) {
    feedback.subject[0]=feedback.message[0]=0;request_id[0]=0;
    free(last_payload);last_payload=NULL;
    status("Thank you. Your message was sent.");
  } else {
    const char *error=json_string_value(json_object_get(j,"error"));
    status(error?error:"Could not confirm delivery. Retry sends the same request.");
  }
  json_decref(j);free(r.body);
}
void feedback_send(void) {
  feedback_open();
  if(job)return;
  if(!net_online() || !*net_token()){status("Connect to the hotel before sending.");return;}
  trim(feedback.subject);trim(feedback.message);
  int a=chars(feedback.subject),b=chars(feedback.message);
  if(a<3 || a>100){status("Use a subject of 3 to 100 characters.");return;}
  if(b<10 || b>2000){status("Use a description of 10 to 2000 characters.");return;}
  static const char *categories[]={"suggestion","bug","other"};
  if(feedback.category<0 || feedback.category>2)feedback.category=0;
  json_t *j=json_pack("{s:s,s:s,s:s}","category",categories[feedback.category],"subject",feedback.subject,"message",feedback.message);
  char *draft=j?json_dumps(j,JSON_COMPACT|JSON_SORT_KEYS):NULL;
  if(!draft){json_decref(j);status("Not enough memory. Your draft is still here.");return;}
  if(!last_payload || strcmp(last_payload,draft)) {
    free(last_payload);last_payload=draft;
    snprintf(request_id,sizeof request_id,"%016llx%08x%08x",(unsigned long long)now_ms(),(unsigned)rand(),++sequence);
  } else free(draft);
  json_object_set_new(j,"request_id",json_string(request_id));
  char *body=json_dumps(j,JSON_COMPACT);json_decref(j);
  job=body?net_http("POST","/api/feedback",body,true):0;free(body);
  feedback.busy=job!=0;
  if(job)snprintf(job_identity,sizeof job_identity,"%s",net_token());
  status(job?"Sending your message...":"Could not send. Your draft is still here.");
}
