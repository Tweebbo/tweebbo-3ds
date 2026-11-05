#include "wired_visuals.h"
#include "wired_visual_source.h"
#include "world.h"
#include "scene.h"
#include "gfx.h"
#include <jansson.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {CONFIGS=25,STATUSES=1000,VISUALS=640,HEADS=1024,LABELS=256,NONE=65535};
typedef struct {u32 id,color,alpha_start,strength_start;u8 alpha,kind,strength,flags,used,alpha_from,strength_from,alpha_easing,strength_easing;} Visual;
typedef struct {int64_t min,max;u32 id,color,extra_color;int32_t order;u16 duration;u8 category,renderer,width,segments,show_mode,update_mask;bool used,user,hover;char icon[24];} Config;
typedef struct {int64_t value,min,max;u32 entity,changed,identity_a,identity_b,delegated;u16 next,level,max_level;u8 flags,config;} Status;
typedef struct {u32 entity;u16 first;bool user;} Head;
typedef struct {u32 item,color;bool always_open;char text[136];} Label; /* four wrapped lines; 256 labels keep the memory budget */
#ifdef WIRED_VISUAL_TEST
size_t wired_visuals_test_status_visits;
#define COUNT_STATUS() (wired_visuals_test_status_visits++)
#else
#define COUNT_STATUS() ((void)0)
#endif
static struct {Visual visual[VISUALS];u16 visual_index[1024];Config config[CONFIGS];Status status[STATUSES];Head head[HEADS];Label label[LABELS];u16 label_index[1024];u16 label_count,visual_count;} state;
_Static_assert(sizeof state<=131072,"Wired visuals memory budget");
static const char *text(json_t *o,const char *name){const char *s=json_string_value(json_object_get(o,name));return s?s:"";}
static u32 id(json_t *v){json_int_t n=json_integer_value(v);return json_is_integer(v)&&n>0&&n<=UINT32_MAX?(u32)n:0;}
static int number(json_t *o,const char *name){json_int_t n=json_integer_value(json_object_get(o,name));return n<INT32_MIN?INT32_MIN:n>INT32_MAX?INT32_MAX:(int)n;}
static bool decimal(json_t *value,int64_t *out){const char *s=json_string_value(value);if(!s||!*s)return false;const char *p=*s=='-'?s+1:s;if(!*p)return false;for(;*p;p++)if(*p<'0'||*p>'9')return false;errno=0;char *end;long long n=strtoll(s,&end,10);if(errno||*end)return false;*out=(int64_t)n;return true;}
static u32 hex(const char *value){if(!value||value[0]!='#'||strlen(value)!=7)return 0;char *end;unsigned long c=strtoul(value+1,&end,16);return *end?0:(u32)c|0xff000000;}
static u32 rgba(u32 rgb,u8 alpha){return (u32)alpha<<24|((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);}
static u32 hash(bool user,u32 key){return (key*2654435761u^(user?0x9e3779b9u:0))&1023;}
static Visual *visual(bool user,u32 key,bool create){
  if(!key)return NULL;
  int reusable=-1;u32 at=hash(user,key);
  for(int i=0;i<1024;i++,at=(at+1)&1023){u16 slot=state.visual_index[at];if(slot&&slot!=NONE){Visual *v=&state.visual[slot-1];if(v->id==key&&(bool)(v->flags&1)==user)return v;}if((!slot||slot==NONE)&&reusable<0)reusable=(int)at;if(!slot)break;}
  if(!create||reusable<0||state.visual_count>=VISUALS)return NULL;
  for(int i=0;i<VISUALS;i++)if(!state.visual[i].used){Visual *v=&state.visual[i];*v=(Visual){.id=key,.alpha=255,.alpha_from=255,.flags=user?1:0,.used=1};state.visual_index[reusable]=(u16)(i+1);state.visual_count++;return v;}
  return NULL;
}
static unsigned elapsed(u64 now,u32 start){return now<start?0:now-start>UINT32_MAX?UINT32_MAX:(unsigned)(now-start);}
static float visual_alpha(Visual *v,u64 now){return v->alpha_easing?v->alpha_from+((float)v->alpha-v->alpha_from)*wired_visual_ease(elapsed(now,v->alpha_start)/500.f,v->alpha_easing):v->alpha;}
static unsigned visual_strength(Visual *v,u64 now){return wired_visual_quantize(wired_visual_strength(v->strength,v->strength_from,elapsed(now,v->strength_start),(v->flags&4)!=0,v->strength_easing));}

static int config_index(u32 key){for(int i=0;i<CONFIGS;i++)if(state.config[i].used&&state.config[i].id==key)return i;return -1;}
static Head *head(bool user,u32 entity,bool create){if(!entity)return NULL;u32 at=hash(user,entity);for(int i=0;i<HEADS;i++,at=(at+1)&1023){Head *h=&state.head[at];if(h->entity==entity&&h->user==user)return h;if(!h->entity){if(!create)return NULL;*h=(Head){.entity=entity,.first=NONE,.user=user};return h;}}return NULL;}
static void rebuild(void){memset(state.head,0,sizeof state.head);for(int i=0;i<STATUSES;i++)if(state.status[i].flags&1){Status *s=&state.status[i];Head *h=head((s->flags&8)!=0,s->entity,true);if(h){s->next=h->first;h->first=(u16)i;}}}
static void rebuild_labels(void){memset(state.label_index,0,sizeof state.label_index);for(int i=0;i<state.label_count;i++){u32 at=hash(false,state.label[i].item);for(int probe=0;probe<1024;probe++,at=(at+1)&1023)if(!state.label_index[at]){state.label_index[at]=(u16)(i+1);break;}}}
static Label *label_for(u32 entity){u32 at=hash(false,entity);for(int probe=0;probe<1024;probe++,at=(at+1)&1023){u16 slot=state.label_index[at];if(!slot)return NULL;if(state.label[slot-1].item==entity)return &state.label[slot-1];}return NULL;}
void wired_visuals_reset(void){memset(&state,0,sizeof state);}
size_t wired_visuals_bytes(void){return sizeof state;}
void wired_visuals_forget(bool user,u32 key){Visual *v=visual(user,key,false);if(v){u32 at=hash(user,key);for(int i=0;i<1024;i++,at=(at+1)&1023)if(state.visual_index[at]&&state.visual_index[at]!=NONE&& &state.visual[state.visual_index[at]-1]==v){state.visual_index[at]=NONE;break;}v->used=0;state.visual_count--;}for(int i=0;i<STATUSES;i++)if((state.status[i].flags&1)&&((state.status[i].flags&8)!=0)==user&&state.status[i].entity==key)state.status[i].flags=0;if(!user)for(int i=0;i<state.label_count;i++)if(state.label[i].item==key){memmove(&state.label[i],&state.label[i+1],(state.label_count-i-1)*sizeof(Label));state.label_count--;break;}rebuild_labels();rebuild();}
u8 wired_visuals_alpha(bool user,u32 key,u64 now){Visual *v=visual(user,key,false);return v?(u8)floorf(visual_alpha(v,now)+.5f):255;}
bool wired_visuals_click_through(bool user,u32 key){Visual *v=visual(user,key,false);return v&&(v->flags&2);}
bool wired_visuals_highlight(bool user,u32 id,u64 now,int *kind,u32 *rgb,int *strength){Visual *v=visual(user,id,false);if(!v)return false;unsigned value=visual_strength(v,now);if(!value)return false;if(kind)*kind=v->kind;if(rgb)*rgb=v->color;if(strength)*strength=(int)value;return true;}
u32 wired_visuals_tint(bool user,u32 key,u64 now){Visual *v=visual(user,key,false);if(!v)return 0xffffffff;if(v->kind==3){float t=visual_strength(v,now)/100.f*(v->color?1.35f:1),a=.65f*t;u32 out=0xff000000;for(int shift=0;shift<24;shift+=8){float channel=(v->color>>shift)&255;u32 factor=(u32)floorf(255*(1-a+a*channel/255)+.5f);out|=(factor>255?255:factor)<<(16-shift);}return out;}if(v->kind!=2)return 0xffffffff;u32 c=(u32)floorf(255*(1-.65f*visual_strength(v,now)/100.f)+.5f);return 0xff000000|c<<16|c<<8|c;}
static bool current(const Status *s){if(s->flags&8){Ent *e=world_ent(s->entity);return e&&e->guest==s->identity_a;}Item *i=world_item(s->entity);return i&&i->def==s->identity_a&&i->owner==s->identity_b;}
static void prefix(char *out,size_t capacity,const char *source){size_t n=strlen(source);if(n<capacity){memcpy(out,source,n+1);return;}n=capacity-4;while(n>0&&((unsigned char)source[n]&0xc0)==0x80)n--;memcpy(out,source,n);memcpy(out+n,"...",4);}
bool wired_visuals_receive(const char *raw){
  json_t *o=json_loads(raw,0,NULL);if(!o)return false;const char *kind=text(o,"type");
  if(!strcmp(kind,"wired_visibility")||!strcmp(kind,"wired_highlight")){
    bool user=json_is_true(json_object_get(o,"users"));json_t *ids=json_object_get(o,"ids");for(size_t i=0;i<json_array_size(ids)&&i<100;i++){u32 key=id(json_array_get(ids,i));if(user?!world_ent(key):!world_item(key))continue;Visual *v=visual(user,key,true);if(!v)continue;
      u64 now=now_ms();bool initialize=json_is_true(json_object_get(o,"initialize"));
      if(!strcmp(kind,"wired_visibility")){v->alpha_from=(u8)floorf(visual_alpha(v,now)+.5f);int transparency=number(o,json_object_get(o,"transparency")?"transparency":"visibility");if(transparency<0)transparency=0;if(transparency>100)transparency=100;v->alpha=(u8)floorf(255*(1-transparency/100.f)+.5f);v->alpha_start=(u32)now;v->alpha_easing=initialize?0:(u8)number(o,"easing");v->flags=(v->flags&~2)|(json_is_true(json_object_get(o,"clickThrough"))?2:0);}
      else{v->strength_from=(u8)wired_visual_strength(v->strength,v->strength_from,elapsed(now,v->strength_start),(v->flags&4)!=0,v->strength_easing);int strength=number(o,"strength"),type=number(o,"kind");v->kind=(u8)(type<0?0:type>5?5:type);v->strength=(u8)(strength<0?0:strength>100?100:strength);v->color=(u32)number(o,"color")&0xffffff;v->flags=(v->flags&~4)|(number(o,"animation")==1?4:0);v->strength_start=(u32)now;v->strength_easing=initialize||(v->flags&4)?0:(u8)number(o,"easing");}
    }json_decref(o);return true;
  }
  if(!strcmp(kind,"wired_labels")){
    if(json_is_true(json_object_get(o,"replace")))state.label_count=0;
    json_t *labels=json_object_get(o,"labels");for(size_t i=0;i<json_array_size(labels)&&state.label_count<LABELS;i++){json_t *value=json_array_get(labels,i);u32 item=id(json_object_get(value,"item"));if(!item||!world_item(item))continue;Label *label=&state.label[state.label_count++];label->item=item;label->color=(u32)number(value,"color")&0xffffff;label->always_open=json_is_true(json_object_get(value,"alwaysOpen"));prefix(label->text,sizeof label->text,text(value,"text"));}rebuild_labels();json_decref(o);return true;
  }
  if(strcmp(kind,"wired_variable_fx")){json_decref(o);return false;}
  if(json_is_true(json_object_get(o,"reset"))){memset(state.config,0,sizeof state.config);memset(state.status,0,sizeof state.status);}
  json_t *removed=json_object_get(o,"removeConfigs");for(size_t i=0;i<json_array_size(removed);i++){const char *s=json_string_value(json_array_get(removed,i));int slot=config_index(s?(u32)strtoul(s,NULL,10):id(json_array_get(removed,i)));if(slot<0)continue;state.config[slot].used=false;for(int j=0;j<STATUSES;j++)if(state.status[j].config==slot)state.status[j].flags=0;}
  removed=json_object_get(o,"remove");for(size_t i=0;i<json_array_size(removed);i++){const char *s=json_string_value(json_array_get(removed,i));unsigned c,t,e;if(!s||sscanf(s,"%u:%u:%u",&c,&t,&e)!=3)continue;int slot=config_index(c);for(int j=0;j<STATUSES;j++)if((state.status[j].flags&1)&&state.status[j].config==slot&&state.status[j].entity==e&&((state.status[j].flags&8)!=0)==(t==0))state.status[j].flags=0;}
  json_t *configs=json_object_get(o,"configs");for(size_t i=0;i<json_array_size(configs);i++){json_t *value=json_array_get(configs,i);u32 key=id(json_object_get(value,"id"));if(!key)continue;int slot=config_index(key);if(slot<0)for(int j=0;j<CONFIGS;j++)if(!state.config[j].used){slot=j;break;}if(slot<0)continue;int64_t min,max;if(!decimal(json_object_get(value,"min"),&min)||!decimal(json_object_get(value,"max"),&max))continue;json_t *extra=json_object_get(value,"extra");Config *c=&state.config[slot];*c=(Config){.id=key,.min=min,.max=max,.color=(u32)number(value,"color"),.extra_color=hex(text(extra,"color")),.order=(int32_t)strtol(text(extra,"stack_order"),NULL,10),.duration=(u16)number(value,"duration"),.show_mode=(u8)number(value,"showMode"),.update_mask=(u8)number(value,"updateMask"),.hover=json_is_true(json_object_get(value,"hover")),.category=(u8)number(value,"category"),.renderer=(u8)number(value,"renderer"),.width=(u8)number(value,"width"),.segments=(u8)strtoul(text(extra,"segments"),NULL,10),.used=true,.user=json_is_true(json_object_get(value,"user"))};snprintf(c->icon,sizeof c->icon,"%s",text(extra,"icon"));}
  json_t *updates=json_object_get(o,"statuses");for(size_t i=0;i<json_array_size(updates);i++){json_t *value=json_array_get(updates,i);u32 entity=id(json_object_get(value,"entity"));int slot=config_index(id(json_object_get(value,"config")));int64_t number_value;if(!entity||slot<0||!decimal(json_object_get(value,"value"),&number_value))continue;bool user=json_is_true(json_object_get(value,"user"));if(user!=state.config[slot].user)continue;int found=-1,free_slot=-1;for(int j=0;j<STATUSES;j++){Status *s=&state.status[j];if(!(s->flags&1)){if(free_slot<0)free_slot=j;}else if(s->config==slot&&s->entity==entity&&((s->flags&8)!=0)==user){found=j;break;}}if(found<0)found=free_slot;if(found<0)continue;
    json_t *identity=json_object_get(value,"identity"),*extra=json_object_get(value,"extra");Status next={.value=number_value,.entity=entity,.changed=(u32)now_ms(),.identity_a=(u32)json_integer_value(json_array_get(identity,0)),.identity_b=(u32)json_integer_value(json_array_get(identity,1)),.flags=(u8)(1|(user?8:0)|(json_is_true(json_object_get(extra,"is_maxed"))||!strcmp(text(extra,"is_maxed"),"true")?16:0)),.config=(u8)slot,.level=(u16)strtoul(text(extra,"current_level"),NULL,10),.max_level=(u16)strtoul(text(extra,"max_level"),NULL,10),.delegated=hex(text(extra,"delegated_color")),.next=NONE};if(decimal(json_object_get(value,"min"),&next.min))next.flags|=2;if(decimal(json_object_get(value,"max"),&next.max))next.flags|=4;Config *display=&state.config[slot];Status *previous=&state.status[found];unsigned change=!(previous->flags&1)?1:next.value>previous->value?2:next.value<previous->value?4:8;next.changed=previous->flags&1?previous->changed:0;if(!json_is_true(json_object_get(value,"initialize"))&&display->show_mode==1&&(display->update_mask&change))next.changed=(u32)now_ms()+display->duration;if(current(&next))state.status[found]=next;
  }rebuild();json_decref(o);return true;
}
static float fraction(const Status *s,const Config *c){long double min=s->flags&2?s->min:c->min,max=s->flags&4?s->max:c->max;if(max<=min)return s->value>=max?1:0;long double f=((long double)s->value-min)/(max-min);return f<0?0:f>1?1:(float)f;}
static u32 color(const Status *s,const Config *c,float f){
  static const u32 colors[]={0x36b24a,0x8ddc35,0xffd83d,0xff9f24,0xdf291e,0x42d4e8,0x3b7de3,0x8751d1,0xff6fb3,0x8a533a,0xd6c28c,0x2fb7a8,0x4d5bd7,0xd64bc1,0x82cfff,0xff5a1f,0x1f6f3a,0x1f3d8f,0xd8d8d8};
  if(s->delegated)return rgba(s->delegated,255);
  if(c->extra_color)return rgba(c->extra_color,255);
  u32 rgb=0x36b24a;
  if(c->color>=1&&c->color<=19)rgb=colors[c->color-1];else switch(c->color){case 100:rgb=0xcd7f32;break;case 101:rgb=0xc0c0c0;break;case 102:rgb=0xffc83d;break;case 103:rgb=0x8eeaff;break;case 104:rgb=0x27c46b;break;case 1001:{double t=s->max_level<=1?(s->flags&16?1:0):fmax(0,fmin(1,((double)s->level-1)/(s->max_level-1))),h=fmod(115+245*t,360)/60,a=.8*.85,x=a*(1-fabs(fmod(h,2)-1)),m=.8-a;double r=h<1?a:h<2?x:h<4?0:h<5?x:a,g=h<1?x:h<3?a:h<4?x:0,b=h<2?0:h<3?x:h<5?a:x;rgb=(u32)floor((r+m)*255)<<16|(u32)floor((g+m)*255)<<8|(u32)floor((b+m)*255);break;}case 1000:rgb=f<1.f/3?15672088:f<2.f/3?16554256:904478;break;case 1002:rgb=14211288;break;}return rgba(rgb,255);
}
static float display(const Status *s,const Config *c,float x,float y,float k){
  float f=fraction(s,c),width=c->width<5?(float[]){32,48,64,80,96}[c->width]:128;u32 fill=color(s,c,f);char caption[96];
  if(c->category==5||c->renderer>=200){snprintf(caption,sizeof caption,"%s%s%lld",c->icon,*c->icon?" ":"",(long long)s->value);float w=text_width(caption,10)+10;fill_rect(x-w*k/2,y-15*k,w*k,15*k,0xff2a2317);text_draw(caption,x,y-13*k,10*k,fill,1);return 18*k;}
  if(c->renderer==11){static const u8 rows[]={54,127,127,127,62,28,8};for(int row=0;row<7;row++)for(int col=0;col<7;col++)if(rows[row]&(1<<col))fill_rect(x+(col-3.5f)*3*k,y+(row-7)*3*k,3*k,3*k,row>=7-(int)(f*7+.5f)?fill:0xff3b3427);return 25*k;}
  if(c->renderer==13){fill_rect(x-7*k,y-29*k,14*k,29*k,0xff2a2317);fill_rect(x-4*k,y-(4+f*22)*k,8*k,f*22*k,fill);snprintf(caption,sizeof caption,"%lld",(long long)s->value);text_draw(caption,x+12*k,y-15*k,9*k,0xffffffff,0);return 32*k;}
  bool mini=c->renderer==1;float height=mini?5:13;if(c->category==4&&width<128)width=128;int segments=c->segments?c->segments:(c->renderer==2||c->renderer==12)?10:0;if(segments>100)segments=100;
  fill_rect(x-(width/2+1)*k,y-(height+1)*k,(width+2)*k,(height+2)*k,0xff2a2317);
  if(segments){float step=width/segments;for(int i=0;i<segments;i++){float part=f*segments-i;if(part>1)part=1;if(part>0)fill_rect(x+(i*step-width/2)*k,y-height*k,(step-fminf(1,step*.2f))*part*k,height*k,fill);}}
  else fill_rect(x-width*k/2,y-height*k,width*f*k,height*k,fill);
  if(c->renderer==3)for(float stripe=0;stripe<width*f;stripe+=7)fill_rect(x+(stripe-width/2)*k,y-height*k,k,height*k,0x70ffffff);
  if(c->renderer==4&&f>0)for(int row=0;row<3;row++)fill_rect(x+(width*f-width/2-3+row)*k,y-(height/2+2-row)*k,k,(5-2*row)*k,0xffffffff);
  if(!mini){if(c->category==2)snprintf(caption,sizeof caption,"L%u %lld",s->level?s->level:1,(long long)s->value);else snprintf(caption,sizeof caption,"%s%s%lld",c->category==3?c->icon:"",c->category==3&&*c->icon?" ":"",(long long)s->value);float size=9;while(size>6&&text_width(caption,size)>width-4)size--;text_draw(caption,x,y-height*k+(height-size)*k/2,size*k,0xffffffff,1);}return (height+5)*k;
}
/* Room labels are queued while the room is drawn and painted afterwards, so furniture in front
   never covers them and they can be kept inside the visible room area (beside open windows). */
#define QUEUED_LABELS 16
typedef struct {const Label *label;float x,y,k;u64 now;} QueuedLabel;
static QueuedLabel queued_label[QUEUED_LABELS];static int queued_label_count;static u64 queued_label_now;
/* Wraps on spaces: a word only moves to the next line when it does not fit. */
static int label_lines(const char *text,char line[4][40],float max_w,float size){
  int lines=0;const char *p=text;
  while(*p&&lines<4){
    while(*p==' ')p++;
    if(!*p)break;
    int len=0,fit=0;const char *q=p;
    while(*q&&*q!='\n'&&len<39){
      line[lines][len++]=*q;line[lines][len]=0;
      if(text_width(line[lines],size)>max_w){len--;break;}
      q++;if(*q==' '||*q=='\n'||!*q)fit=len;
    }
    if(*q&&*q!='\n'&&*q!=' '&&fit>0){len=fit;q=p+fit;}
    else if(len&&((unsigned char)*q&0xc0)==0x80){while(len>0&&((unsigned char)p[len]&0xc0)==0x80)len--;q=p+len;}
    if(!len){len=1;q=p+1;}
    line[lines][len]=0;while(len>0&&line[lines][len-1]==' ')line[lines][--len]=0;
    lines++;p=q;if(*p=='\n')p++;
  }
  return lines;
}
void wired_visuals_draw_labels(float view_left,float view_top,float view_right,float view_bottom){
  int count=queued_label_count;queued_label_count=0;
  float view_w=view_right-view_left-4,view_h=view_bottom-view_top-4;if(view_w<60||view_h<24)return;
  for(int i=0;i<count;i++){QueuedLabel *q=&queued_label[i];if(q->now!=queued_label_now)continue;const Label *label=q->label;float k=q->k;
    char line[4][40]={{0}};float text_max=fminf(180,view_w/k-12);int lines=label_lines(label->text,line,text_max,10);if(!lines)continue;
    float width=40;for(int row=0;row<lines;row++){float w=text_width(line[row],10)+12;if(w>width)width=w;}
    float box_w=fminf(width*k,view_w),box_h=fminf((lines*12+10)*k,view_h);
    float left=fmaxf(view_left+2,fminf(q->x-box_w/2,view_right-box_w-2)),top=fmaxf(view_top+2,fminf(q->y-box_h,view_bottom-box_h-2));
    fill_rect(left,top,box_w,box_h,rgba(label->color,255));
    for(int row=0;row<lines;row++){text_fit(line[row],10*k,fmaxf(0,box_w-12*k));text_draw(line[row],left+6*k,top+(5+row*12)*k,10*k,0xffffffff,0);}
  }
}
void wired_visuals_draw(bool user,u32 entity,float x,float y,float k,bool hover,u64 now){
  if(!wired_visuals_alpha(user,entity,now))return;
  Visual *v=visual(user,entity,false);unsigned strength=v?visual_strength(v,now):0;if(v&&strength&&v->kind!=2&&v->kind!=3&&v->kind!=4){u32 c=rgba(v->kind==4?0xffffff:v->color,(u8)(strength*1.4f));float w=(user?40:50)*k,h=(user?58:34)*k;fill_rect(x-w/2,y-2*k,w,k,c);fill_rect(x-w/2,y+h,w,k,c);fill_rect(x-w/2,y-k,k,h+k,c);fill_rect(x+w/2-k,y-k,k,h+k,c);}
  Head *h=head(user,entity,false);u16 rows[25];int n=0;if(h)for(u16 at=h->first;at!=NONE&&n<25;at=state.status[at].next){Status *s=&state.status[at];COUNT_STATUS();Config *display=&state.config[s->config];if(!current(s)||!display->used||!(display->show_mode==0||(display->hover&&hover)||(display->show_mode==1&&s->changed>now)))continue;int pos=n;Config *current_config=&state.config[s->config];while(pos>0){Config *previous=&state.config[state.status[rows[pos-1]].config];if(previous->order>current_config->order||(previous->order==current_config->order&&previous->id<current_config->id))break;rows[pos]=rows[pos-1];pos--;}rows[pos]=at;n++;}
  float offset=3*k;for(int i=0;i<n;i++){Status *s=&state.status[rows[i]];offset+=display(s,&state.config[s->config],x,y-offset,k);}
  Label *label=user?NULL:label_for(entity);if(label&&(hover||label->always_open)&&*label->text&&queued_label_count<QUEUED_LABELS){if(queued_label_now!=now){queued_label_now=now;queued_label_count=0;}queued_label[queued_label_count++]=(QueuedLabel){label,x,y-offset,k,now};}
}
