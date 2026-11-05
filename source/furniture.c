#include "furniture.h"
#include "catalog.h"
#include "net.h"
#include "world.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

FurniturePanel furniture_panel;
static json_t *opened;
static char *long_value;
static u32 open_revision;
static int light_alpha;
static u32 light_color;
static bool light_background;
static json_t *arena;
typedef struct {u32 id,color;u64 look;bool has_look;} Appearance;
static Appearance appearances[MAX_ITEMS];
static int appearance_count;
typedef struct {u32 id;char text[24];} ClockText;
static ClockText clocks[MAX_ITEMS];
static int clock_count;
/* Room drawing looks these up for every furniture part on every frame: keep them sorted by id
   (stable, so the first entry for a repeated id still wins) and binary-search them. */
static void sort_appearances(void){for(int i=1;i<appearance_count;i++){Appearance a=appearances[i];int j=i;while(j>0&&appearances[j-1].id>a.id){appearances[j]=appearances[j-1];j--;}appearances[j]=a;}}
static void sort_clocks(void){for(int i=1;i<clock_count;i++){ClockText c=clocks[i];int j=i;while(j>0&&clocks[j-1].id>c.id){clocks[j]=clocks[j-1];j--;}clocks[j]=c;}}
static const Appearance *find_appearance(u32 item){int lo=0,hi=appearance_count;while(lo<hi){int mid=(lo+hi)/2;if(appearances[mid].id<item)lo=mid+1;else hi=mid;}return lo<appearance_count&&appearances[lo].id==item?&appearances[lo]:NULL;}
/* Visible-field order for the editor, rebuilt only when the form or a row count changes. */
static int display_order[FURNITURE_FIELDS],display_total=-1;
static u32 display_revision;static int display_kind,display_fields,display_rows;
static u8 target_kind;
static u8 motion_kind;
static u8 target_mask;
static bool control_hints,control_delay;
static int wired_params,wired_rows,wired_text_parts;
static json_t *wired_choices(int index);
static const char *string(json_t *o,const char *key);
static bool wired_current_outfit(int index){
  if(furniture_panel.kind!=F_WIRED||index<3||index>=3+wired_params)return false;
  json_t *schema=json_object_get(json_object_get(opened,"wired"),"parameters");return !strcmp(string(json_array_get(schema,(size_t)(index-3)),"action"),"currentOutfit");
}
static u16 gift_offer;static int gift_wrapper;static char gift_key[64];
static void gift_total(void);
static const char *string(json_t *o,const char *key){const char *s=json_string_value(json_object_get(o,key));return s?s:"";}
static int number(json_t *o,const char *key){return (int)json_integer_value(json_object_get(o,key));}
static bool send(json_t *o){char *s=json_dumps(o,JSON_COMPACT);bool sent=false;if(s){if(strlen(s)>16384)snprintf(furniture_panel.status,sizeof furniture_panel.status,"Too much text to send. Shorten the longer fields.");else{send_furniture(s);sent=true;}free(s);}json_decref(o);return sent;}
static void value_field(const char *key,const char *label,const char *value,int limit,bool edit,size_t capacity){
  FurniturePanel *p=&furniture_panel;if(p->field_count>=FURNITURE_FIELDS){p->editable=false;snprintf(p->status,sizeof p->status,"This furniture form has too many settings to edit.");return;}
  size_t initial=strlen(value?value:"")+1;if(capacity<initial)capacity=initial;
  bool external=capacity>4097;
  if((external&&(capacity>8193||long_value))||(!external&&capacity>sizeof p->values-p->values_used)){p->editable=false;snprintf(p->status,sizeof p->status,"This furniture form is too large to edit.");return;}
  if(external&&!(long_value=malloc(capacity))){p->editable=false;snprintf(p->status,sizeof p->status,"Not enough memory to edit this text.");return;}
  FurnitureField *f=&p->fields[p->field_count++];f->key=key;f->label=label;f->limit=limit;f->editable=edit;
  f->capacity=(u16)capacity;f->value=external?long_value:p->values+p->values_used;if(!external)p->values_used+=(u16)capacity;snprintf(f->value,f->capacity,"%s",value?value:"");
}
static void field(const char *key,const char *label,const char *value,int limit,bool edit){value_field(key,label,value,limit,edit,limit>80?4001:(size_t)limit*4+1);}
static void numeric(const char *key,const char *label,long long value,bool edit){char s[32];snprintf(s,sizeof s,"%lld",value);value_field(key,label,s,20,edit,21);}
static bool height_hundredths(const char *text,int *height){
  if(!text||*text<'0'||*text>'9')return false;
  int whole=0,fraction=0,digits=0;
  while(*text>='0'&&*text<='9'){whole=whole*10+*text++-'0';if(whole>40)return false;}
  if(*text=='.'){text++;while(*text>='0'&&*text<='9'){if(++digits>2)return false;fraction=fraction*10+*text++-'0';}if(!digits)return false;}
  if(*text)return false;if(digits==1)fraction*=10;
  *height=whole*100+fraction;return *height<=4000;
}
static void height_value(char *out,size_t capacity,int height){snprintf(out,capacity,"%d.%02d",height/100,height%100);}
static const char *height_label(json_t *data){
  FurniturePanel *p=&furniture_panel;if(p->values_used<48)p->values_used=48;
  int minimum=number(data,"minimum");snprintf(p->values,48,"Height (%d.%02d-40.00)",minimum/100,minimum%100);return p->values;
}
void furniture_reset(void){free(long_value);long_value=NULL;json_decref(opened);opened=NULL;json_decref(arena);arena=NULL;memset(&furniture_panel,0,sizeof furniture_panel);light_alpha=0;appearance_count=0;clock_count=0;motion_kind=0;target_kind=0;target_mask=0;control_hints=false;control_delay=false;wired_params=0;wired_rows=0;wired_text_parts=0;}
const char *furniture_clock_text(u32 item){int lo=0,hi=clock_count;while(lo<hi){int mid=(lo+hi)/2;if(clocks[mid].id<item)lo=mid+1;else hi=mid;}return lo<clock_count&&clocks[lo].id==item?clocks[lo].text:NULL;}
void furniture_open(u32 item){send(json_pack("{s:s,s:I}","op","open","item",(json_int_t)item));}
u32 furniture_light_color(void){return light_color;}
u8 furniture_light_alpha(void){return (u8)light_alpha;}
bool furniture_light_background(void){return light_background;}

void furniture_receive(const char *text){
  json_error_t error;json_t *o=json_loads(text,0,&error);if(!o)return;
  if(!strcmp(string(o,"type"),"wired_web_api_key")){
    FurniturePanel *p=&furniture_panel;const char *key=string(o,"key");int part=json_is_true(json_object_get(o,"write"))?1:0;
    if(p->kind==F_WIRED&&p->item==(u32)json_integer_value(json_object_get(o,"item"))&&strlen(key)==43&&wired_text_parts==2){
      int at=3+wired_params+part;if(at<p->field_count){snprintf(p->fields[at].value,p->fields[at].capacity,"%s",key);json_t *wired=json_object_get(opened,"wired"),*parts=json_object_get(wired,"textFields");json_object_set_new(json_array_get(parts,(size_t)part),"value",json_string(key));char joined[88];snprintf(joined,sizeof joined,"%s\t%s",p->fields[3+wired_params].value,p->fields[4+wired_params].value);json_object_set_new(json_object_get(wired,"config"),"text",json_string(joined));snprintf(p->status,sizeof p->status,"New API key received in this window.");}
    }json_decref(o);return;
  }
  const char *op=string(o,"op");
  if(!strcmp(op,"clocks")){
    clock_count=0;json_t *items=json_object_get(o,"items");
    for(size_t i=0;i<json_array_size(items)&&clock_count<MAX_ITEMS;i++){
      json_t *it=json_array_get(items,i);int at=clock_count++;
      clocks[at].id=(u32)json_integer_value(json_object_get(it,"id"));snprintf(clocks[at].text,sizeof clocks[at].text,"%s",string(it,"text"));
    }
    sort_clocks();json_decref(o);return;
  }
  if(!strcmp(op,"state")){
    json_decref(arena);arena=json_deep_copy(json_object_get(o,"arena"));
    json_t *light=json_object_get(o,"light");light_color=(u32)strtoul(string(light,"color")+(*string(light,"color")=='#'),NULL,16);
    int intensity=number(light,"intensity");
    light_color=(((light_color>>16&255)*intensity/255)<<16)|(((light_color>>8&255)*intensity/255)<<8)|((light_color&255)*intensity/255);
    light_alpha=json_is_true(json_object_get(light,"enabled"))?255-intensity:0;
    light_background=json_is_true(json_object_get(light,"backgroundOnly"));json_decref(o);return;
  }
  if(!strcmp(op,"appearance")){
    appearance_count=0;json_t *items=json_object_get(o,"items");
    for(size_t i=0;i<json_array_size(items)&&appearance_count<MAX_ITEMS;i++){
      json_t *it=json_array_get(items,i);int at=appearance_count++;
      appearances[at].id=(u32)json_integer_value(json_object_get(it,"id"));const char *color=string(it,"color");
      appearances[at].color=*color=='#'?(u32)strtoul(color+1,NULL,16):0xffffff;
      json_t *look=json_object_get(it,"look");appearances[at].has_look=json_is_integer(look);appearances[at].look=(u64)json_integer_value(look);
    }
    sort_appearances();json_decref(o);return;
  }
  if(!strcmp(op,"saved")||!strcmp(op,"wired_saved")){
    if(!strcmp(string(o,"kind"),"magic_tile")){
      FurniturePanel *p=&furniture_panel;
      if(p->kind!=F_MAGIC_TILE||(u32)json_integer_value(json_object_get(o,"item"))!=p->item){json_decref(o);return;}
      json_t *data=json_object_get(o,"data");
      if(json_is_integer(json_object_get(data,"height"))&&p->field_count>=2){
        height_value(p->fields[0].value,p->fields[0].capacity,number(data,"height"));height_label(data);snprintf(p->fields[1].value,p->fields[1].capacity,"0");
        json_object_set_new(opened,"data",json_deep_copy(data));
      }
    }
#ifdef TWEEBBO_DEBUG
    if(!strcmp(op,"wired_saved"))dbg_log("Furniture fixture saved %u",furniture_panel.item);
#endif
    snprintf(furniture_panel.status,sizeof furniture_panel.status,"%s",*string(o,"message")?string(o,"message"):"Settings saved.");json_decref(o);return;
  }
  if(strcmp(op,"open")){json_decref(o);return;}
  free(long_value);long_value=NULL;json_decref(opened);opened=o;target_kind=0;motion_kind=0;target_mask=0;control_hints=false;control_delay=false;wired_params=0;wired_rows=0;wired_text_parts=0;memset(&furniture_panel,0,sizeof furniture_panel);FurniturePanel *p=&furniture_panel;p->revision=++open_revision;
  const char *kind=string(o,"kind");const char *kinds[]={"note","trophy","moodlight","gift","mannequin","changing","wired","freeze","banzai"};
  for(int i=0;i<9;i++)if(!strcmp(kind,kinds[i]))p->kind=i+1;
  if(!strcmp(kind,"clock"))p->kind=F_CLOCK;
  if(!strcmp(kind,"football_gate"))p->kind=F_FOOTBALL_GATE;
  if(!strcmp(kind,"magic_tile"))p->kind=F_MAGIC_TILE;
  p->item=(u32)json_integer_value(json_object_get(o,"item"));p->definition=(u16)number(o,"definition");p->editable=json_is_true(json_object_get(o,"editable"));p->owned=json_is_true(json_object_get(o,"owned"));
  p->pending=true;snprintf(p->name,sizeof p->name,"%s",string(o,"name"));json_t *data=json_object_get(o,"data");
#ifdef TWEEBBO_DEBUG
  dbg_log("Furniture fixture opened %u (%s)",(unsigned)p->item,string(o,"kind"));
#endif
  p->can_apply=p->kind==F_GIFT?(p->owned&&json_is_true(json_object_get(data,"filled"))):((p->kind==F_MANNEQUIN||p->kind==F_CHANGING)&&json_is_integer(json_object_get(data,"look")));
  if(p->kind==F_WIRED&&json_is_false(json_object_get(json_object_get(o,"wired"),"supported"))){
    snprintf(p->status,sizeof p->status,"This behavior is unavailable in this version.");
    json_t *ops=json_object_get(json_object_get(o,"wired"),"operations");bool any=false;
    for(size_t i=0;i<json_array_size(ops);i++)any|=json_is_true(json_object_get(json_array_get(ops,i),"supported"));
    if(!any)p->editable=false;
  }
  switch(p->kind){
    case F_MAGIC_TILE:{
      if(json_is_false(json_object_get(o,"placed"))){p->editable=false;snprintf(p->status,sizeof p->status,"Place this helper in your room to set its height.");break;}
      char value[24];height_value(value,sizeof value,number(data,"height"));
      field("height",height_label(data),value,8,p->editable);numeric("heightAction","Height action",0,p->editable);
      snprintf(p->status,sizeof p->status,"%s",p->editable?"Set a height or match the stack below.":"You need room rights to change this height.");break;}
    case F_NOTE:field("text","Note",string(data,"text"),1000,p->editable);field("color","Paper color",*string(data,"color")?string(data,"color"):"#FFFF99",7,p->editable);break;
    case F_TROPHY:field("message","Permanent dedication",string(data,"message"),500,p->owned&&!*string(data,"message"));field("sender","From",string(data,"sender"),32,false);break;
    case F_MOODLIGHT:{json_t *light=json_object_get(data,"light");field("color","Light color",string(light,"color"),7,p->editable);numeric("intensity","Brightness (32-255)",number(light,"intensity"),p->editable);numeric("enabled","Light enabled (0/1)",json_is_true(json_object_get(light,"enabled")),p->editable);numeric("backgroundOnly","Background only (0/1)",json_is_true(json_object_get(light,"backgroundOnly")),p->editable);numeric("preset","Preset (1-3)",1,p->editable);break;}
    case F_MANNEQUIN:case F_CHANGING:case F_FOOTBALL_GATE:
      field("name","Outfit name",*string(data,"name")?string(data,"name"):p->name,40,p->editable);
      if(p->kind==F_FOOTBALL_GATE){field("gender","Kit for",string(data,"gender"),16,false);snprintf(p->status,sizeof p->status,"Walk through to wear this kit. Pass again to restore.");}break;
    case F_GIFT:field("message","Gift message",string(data,"message"),500,false);field("sender","From",string(data,"sender"),32,false);break;
    case F_CLOCK:
      field("time","Current time",string(data,"time"),24,false);
      numeric("command","Clock command",0,p->editable);
      numeric("minutes","Minutes (0-99)",0,p->editable);
      numeric("steps","Half-second steps (0-119)",0,p->editable);break;
    case F_WIRED:{
      json_t *wired=json_object_get(o,"wired"),*config=json_object_get(wired,"config");
      p->variable_definition=!strncmp(string(config,"operation"),"wf_var_",7);
      motion_kind=!strcmp(string(config,"operation"),"wf_act_move_rotate_user")?1:!strcmp(string(config,"operation"),"wf_xtra_mov_carry_users")?2:!strcmp(string(config,"operation"),"wf_xtra_mov_physics")?3:0;
      target_kind=motion_kind==1||motion_kind==2?1:0;
      p->compact_motion=motion_kind||!strcmp(string(config,"operation"),"wf_xtra_anim_time")||!strcmp(string(config,"operation"),"wf_xtra_mov_no_animation");
      json_t *controls=json_object_get(wired,"controls");control_hints=json_is_object(controls);
      if(control_hints){
        p->compact_motion=json_is_true(json_object_get(controls,"compact"));control_delay=json_is_true(json_object_get(controls,"delay"));
        json_t *kinds=json_object_get(controls,"targets");
        for(size_t i=0;i<json_array_size(kinds);i++){
          const char *kind=json_string_value(json_array_get(kinds,i));if(!kind)continue;
          if(!strcmp(kind,"primary"))target_mask|=1;else if(!strcmp(kind,"players"))target_mask|=2;else if(!strcmp(kind,"secondary"))target_mask|=4;
        }
        target_kind=target_mask&1?0:target_mask&4?2:1;
      }else target_mask=motion_kind==3?7:motion_kind==1||motion_kind==2?2:3;
      if(*string(wired,"error"))snprintf(p->status,sizeof p->status,"%s",string(wired,"error"));
      field("operation","Behavior",string(config,"operation"),80,false);
      json_t *text_rows=json_object_get(wired,"textRows"),*text_parts=json_object_get(wired,"textFields");bool structured=json_is_array(text_rows)||json_is_array(text_parts);
      int text_limit=number(wired,"textLimit");if(text_limit<1)text_limit=p->variable_definition?40:500;if(text_limit>8192)text_limit=8192;
      value_field("text",*string(wired,"textLabel")?string(wired,"textLabel"):"Message or variable",structured?"":string(config,"text"),structured?0:text_limit,p->editable&&!structured,structured?1:(size_t)text_limit*4+1>8193?8193:(size_t)text_limit*4+1);
      numeric("delay","Delay in half seconds",number(config,"delay"),p->editable&&!p->variable_definition);
      json_t *params=json_object_get(config,"params"),*schema=json_object_get(wired,"parameters");
      int n=(int)json_array_size(params),named=json_is_array(schema)?(int)json_array_size(schema):4;if(n<named)n=named;if(n>FURNITURE_FIELDS-3){p->editable=false;snprintf(p->status,sizeof p->status,"This furniture form has too many settings to edit.");break;}wired_params=n;
      for(int i=0;i<n;i++){
        json_t *setting=json_array_get(schema,(size_t)i),*value=json_array_get(params,(size_t)i);
        numeric("parameter",*string(setting,"label")?string(setting,"label"):"Setting",json_integer_value(value?value:json_object_get(setting,"default")),p->editable);
      }
      wired_rows=(int)json_array_size(text_rows);if(wired_rows>12)wired_rows=12;
      for(int i=0;i<wired_rows;i++){json_t *row=json_array_get(text_rows,(size_t)i);value_field("rowKey",string(row,"keyLabel"),string(row,"key"),40,p->editable,161);bool sending=!strcmp(string(config,"operation"),"wf_act_send_dataframe");value_field("rowText",string(row,"textLabel"),string(row,"text"),sending?200:40,p->editable,sending?201:161);}
      wired_text_parts=(int)json_array_size(text_parts);if(wired_text_parts>17)wired_text_parts=17;
      for(int i=0;i<wired_text_parts;i++){json_t *part=json_array_get(text_parts,(size_t)i);int limit=number(part,"limit");if(limit<1)limit=40;size_t capacity=(size_t)limit*4+1;if(capacity>8193)capacity=8193;value_field("textPart",string(part,"label"),string(part,"value"),limit,p->editable&&!json_is_false(json_object_get(part,"editable")),capacity);}
      if(!strcmp(string(config,"operation"),"wf_xtra_var_web_api")){value_field("apiRead","Generate read key","Generate read key",0,p->editable,18);value_field("apiWrite","Generate write key","Generate write key",0,p->editable,19);}
      if(!strcmp(string(config,"operation"),"wf_xtra_var_share"))value_field("shareSync","Synchronize shared displays","Refresh",0,p->editable,8);
      break;
    }
  }
}

bool furniture_edit(const char *text){
  FurniturePanel *p=&furniture_panel;if(p->selected<0||p->selected>=p->field_count)return false;
  FurnitureField *f=&p->fields[p->selected];if(!f->editable||strlen(text)>=(size_t)f->capacity)return false;
  if(p->kind==F_MAGIC_TILE){
    int height=0;bool valid=p->selected==1?(!strcmp(text,"0")||!strcmp(text,"1")):height_hundredths(text,&height)&&height>=number(json_object_get(opened,"data"),"minimum");
    if(!valid){snprintf(p->status,sizeof p->status,"Enter a height in the shown range with up to two decimal places.");return false;}
  }
  if(p->variable_definition&&!strcmp(f->key,"text")){
    size_t n=strlen(text);bool valid=n>=1&&n<=40;
    for(size_t i=0;i<n;i++)valid&=(text[i]>='a'&&text[i]<='z')||(text[i]>='A'&&text[i]<='Z')||(text[i]>='0'&&text[i]<='9')||text[i]=='_';
    if(!valid){snprintf(p->status,sizeof p->status,"Use 1 to 40 letters, numbers or underscores.");return false;}
  }
  if((p->kind==F_MOODLIGHT&&strcmp(f->key,"color"))||(p->kind==F_WIRED&&(!strcmp(f->key,"parameter")||!strcmp(f->key,"delay")))||p->kind==F_CLOCK){
    char *end;errno=0;long long value=strtoll(text,&end,10);
    if(errno||end==text||*end){snprintf(p->status,sizeof p->status,"Enter a whole number.");return false;}
    json_t *choices=wired_choices(p->selected);
    if(json_array_size(choices)){
      bool found=false;for(size_t i=0;i<json_array_size(choices);i++)found|=json_integer_value(json_object_get(json_array_get(choices,i),"value"))==value;
      if(!found){snprintf(p->status,sizeof p->status,"Choose an available option.");return false;}
    }
    if((p->kind==F_WIRED&&p->selected==2&&(value<0||value>120))||
       (p->kind==F_WIRED&&!strcmp(string(json_object_get(json_object_get(opened,"wired"),"config"),"operation"),"wf_xtra_anim_time")&&p->selected>=3&&(value<50||value>2000))||
       (p->kind==F_MOODLIGHT&&((!strcmp(f->key,"intensity")&&(value<32||value>255))||(!strcmp(f->key,"preset")&&(value<1||value>3))||((!strcmp(f->key,"enabled")||!strcmp(f->key,"backgroundOnly"))&&(value<0||value>1))))||
       (p->kind==F_CLOCK&&(value<0||value>(!strcmp(f->key,"command")?7:!strcmp(f->key,"minutes")?99:119)))){
      snprintf(p->status,sizeof p->status,"Use the range shown in the field label.");return false;
    }
  }
  snprintf(f->value,f->capacity,"%s",text);
  if(p->kind==F_MOODLIGHT&&!strcmp(f->key,"preset")){
    json_t *preset=json_object_get(json_object_get(json_object_get(opened,"data"),"presets"),text);
    if(json_is_object(preset)){
      for(int i=0;i<p->field_count;i++){FurnitureField *target=&p->fields[i];
        if(!strcmp(target->key,"color"))snprintf(target->value,target->capacity,"%s",string(preset,"color"));
        else if(strcmp(target->key,"preset"))snprintf(target->value,target->capacity,"%d",!strcmp(target->key,"enabled")||!strcmp(target->key,"backgroundOnly")?json_is_true(json_object_get(preset,target->key)):number(preset,target->key));
      }
    }
  }
  return true;
}
static void display_build(void){
  FurniturePanel *p=&furniture_panel;int shown=0;
  json_t *wired=p->kind==F_WIRED?json_object_get(opened,"wired"):NULL;
  bool structured=json_is_array(json_object_get(wired,"textRows"))||json_is_array(json_object_get(wired,"textFields"));
  const char *operation=string(json_object_get(wired,"config"),"operation");
  int count=p->field_count>3?atoi(p->fields[3].value):0;
  for(int i=0;i<p->field_count;i++){
    if(p->kind==F_WIRED){
      if(i==1&&structured)continue;
      if(wired_rows){int row=-1,index=i-3;
        if(i>=3+wired_params&&i<3+wired_params+wired_rows*2)row=(i-3-wired_params)/2;
        else if(!strcmp(operation,"wf_act_send_dataframe")){if(index>=1&&index<=36)row=(index-1)/3;else if(index>=37&&index<=48)row=index-37;else if(index>=50&&index<=61)row=index-50;else if(index>=62&&index<=73)row=index-62;}
        else if(!strcmp(operation,"wf_trg_receive_dataframe")){if(index>=1&&index<=12)row=index-1;else if(index>=13&&index<=24)row=index-13;}
        if(row>=0&&row>=count)continue;
      }
    }
    if(p->variable_definition&&(i==0||i==2||!strcmp(p->fields[i].label,"Reserved (0)")))continue;
    if(p->compact_motion&&i<3&&!((control_hints?control_delay:motion_kind==1)&&i==2))continue;
    display_order[shown++]=i;
  }
  display_total=shown;
}
/* The order depends on the opened form (revision), its fields and, for row editors, the row count in field 3. */
static void display_refresh(void){
  FurniturePanel *p=&furniture_panel;int rows=p->field_count>3?atoi(p->fields[3].value):0;
  if(display_total>=0&&display_revision==p->revision&&display_kind==p->kind&&display_fields==p->field_count&&display_rows==rows)return;
  display_revision=p->revision;display_kind=p->kind;display_fields=p->field_count;display_rows=rows;display_build();
}
int furniture_display_index(int visible){display_refresh();return visible>=0&&visible<display_total?display_order[visible]:-1;}
int furniture_display_count(void){display_refresh();return display_total;}
static json_t *wired_choices(int index){
  if(furniture_panel.kind!=F_WIRED||index<3||index>=furniture_panel.field_count)return NULL;
  json_t *schema=json_object_get(json_object_get(opened,"wired"),"parameters");
  json_t *field=json_array_get(schema,(size_t)(index-3));if(!strcmp(string(field,"picker"),"variables"))return json_object_get(json_object_get(opened,"wired"),"variableChoices");
  return json_object_get(field,"choices");
}
bool furniture_has_choices(int index){
  FurniturePanel *p=&furniture_panel;if(index<0||index>=p->field_count)return false;
  if(p->kind==F_WIRED&&(!strcmp(p->fields[index].key,"apiRead")||!strcmp(p->fields[index].key,"apiWrite")||!strcmp(p->fields[index].key,"shareSync")))return true;
  return wired_current_outfit(index)||(p->kind==F_GIFT_BUY&&index==2)||((p->kind==F_NOTE||p->kind==F_CLOCK||p->kind==F_MAGIC_TILE)&&index==1)||json_array_size(wired_choices(index))>0;
}
void furniture_choice(int index,int step){
  FurniturePanel *p=&furniture_panel;if(index<0||index>=p->field_count)return;FurnitureField *f=&p->fields[index];
  if(!f->editable)return;
  if(p->kind==F_WIRED&&(!strcmp(f->key,"apiRead")||!strcmp(f->key,"apiWrite"))){send(json_pack("{s:s,s:I,s:b}","op","wired_web_api_key","item",(json_int_t)p->item,"write",!strcmp(f->key,"apiWrite")));snprintf(p->status,sizeof p->status,"Waiting for the room.");return;}
  if(p->kind==F_WIRED&&!strcmp(f->key,"shareSync")){send(json_pack("{s:s,s:I}","op","wired_share_sync","item",(json_int_t)p->item));snprintf(p->status,sizeof p->status,"Shared display refresh requested.");return;}
  if(wired_current_outfit(index)){snprintf(f->value,f->capacity,"%lld",(long long)json_integer_value(json_object_get(opened,"look")));snprintf(p->status,sizeof p->status,"Current outfit selected.");return;}
  json_t *choices=wired_choices(index);int n=(int)json_array_size(choices);
  if(n){
    int at=step<0?0:-1;long long current=atoll(f->value);
    for(int i=0;i<n;i++)if(json_integer_value(json_object_get(json_array_get(choices,(size_t)i),"value"))==current){at=i;break;}
    int next=(at+step%n+n)%n;snprintf(f->value,f->capacity,"%lld",(long long)json_integer_value(json_object_get(json_array_get(choices,(size_t)next),"value")));return;
  }
  if(p->kind==F_MAGIC_TILE&&index==1){
    snprintf(f->value,f->capacity,"%d",(atoi(f->value)+step%2+2)%2);
  }else if(p->kind==F_NOTE&&index==1){
    static const char *colors[]={"#FFFF99","#99DDFF","#FFBBDD","#BBEEAA","#FFFFFF"};int at=0;
    for(int i=0;i<5;i++)if(!strcmp(f->value,colors[i]))at=i;
    snprintf(f->value,f->capacity,"%s",colors[(at+step+5)%5]);
  }else if(p->kind==F_CLOCK&&index==1){
    snprintf(f->value,f->capacity,"%d",(atoi(f->value)+step+8)%8);
  }else if(p->kind==F_GIFT_BUY&&index==2){
    for(int n=0;n<cat_offer_count;n++){
      gift_wrapper=(gift_wrapper+step+cat_offer_count)%cat_offer_count;
      const CatOffer *o=&cat_offers[gift_wrapper];const FurniDef *d=def_get(o->def);
      if(o->available&&(!o->requires_club||world_club_member())&&d&&d->feature==F_GIFT){snprintf(f->value,f->capacity,"%s",o->name);gift_total();break;}
    }
  }else if(p->kind==F_WIRED&&index==0){
    json_t *ops=json_object_get(json_object_get(opened,"wired"),"operations");int n=(int)json_array_size(ops),at=0;
    for(int i=0;i<n;i++)if(!strcmp(string(json_array_get(ops,i),"name"),f->value))at=i;
    if(n){json_t *selected=json_array_get(ops,(size_t)((at+step+n)%n));snprintf(f->value,f->capacity,"%s",string(selected,"name"));snprintf(p->status,sizeof p->status,"%s",json_is_false(json_object_get(selected,"supported"))?"This behavior is unavailable in this version.":"");}
  }
}
static int target_checked=-1;
const char *furniture_target_label(void){
  static char label[160];json_t *wired=json_object_get(opened,"wired"),*items=json_object_get(wired,target_kind==1?"users":"items");int n=(int)json_array_size(items);
  if(!n){target_checked=-1;return target_kind==1?"No players to select":"No furniture to select";}if(furniture_panel.target<0||furniture_panel.target>=n)furniture_panel.target=0;
  json_t *it=json_array_get(items,(size_t)furniture_panel.target),*targets=json_object_get(json_object_get(wired,"config"),target_kind==1?"users":target_kind==2?"secondary_targets":"targets");json_int_t id=json_integer_value(json_object_get(it,"id"));bool selected=false;
  for(size_t i=0;i<json_array_size(targets);i++)selected|=json_integer_value(json_array_get(targets,i))==id;
  target_checked=selected;snprintf(label,sizeof label,"%s",string(it,"name"));return label;
}
int furniture_target_check(void){furniture_target_label();return target_checked;}
void furniture_target(int step,bool toggle){
  json_t *wired=json_object_get(opened,"wired"),*items=json_object_get(wired,target_kind==1?"users":"items"),*config=json_object_get(wired,"config"),*targets=json_object_get(config,target_kind==1?"users":target_kind==2?"secondary_targets":"targets");int n=(int)json_array_size(items);if(!n)return;
  furniture_panel.target=(furniture_panel.target+step+n)%n;
  if(toggle){if(!json_is_array(targets)){targets=json_array();json_object_set_new(config,target_kind==1?"users":target_kind==2?"secondary_targets":"targets",targets);}json_int_t id=json_integer_value(json_object_get(json_array_get(items,(size_t)furniture_panel.target),"id"));for(size_t i=0;i<json_array_size(targets);i++)if(json_integer_value(json_array_get(targets,i))==id){json_array_remove(targets,i);return;}json_array_append_new(targets,json_integer(id));}
}
const char *furniture_target_kind(void){return target_kind==1?"Players":target_kind==2?"Secondary":(control_hints||motion_kind==3)?"Primary":"Furniture";}
const char *furniture_choice_label(int index){
  FurniturePanel *p=&furniture_panel;if(index<0||index>=p->field_count)return "";
  if(wired_current_outfit(index))return atoll(p->fields[index].value)==json_integer_value(json_object_get(opened,"look"))?"Current outfit selected":"Use current outfit";
  json_t *choices=wired_choices(index);long long current=atoll(p->fields[index].value);
  for(size_t i=0;i<json_array_size(choices);i++){json_t *choice=json_array_get(choices,i);if(json_integer_value(json_object_get(choice,"value"))==current)return string(choice,"label");}
  if(json_array_size(choices))return "Choose an option";
  if(p->kind==F_MAGIC_TILE&&index==1)return atoi(p->fields[index].value)==1?"Match stack below":"Use entered height";
  if(p->kind==F_CLOCK&&index==1){static const char *names[]={"Start","Stop","Reset","Pause","Resume","Increase time","Decrease time","Set time"};int command=atoi(p->fields[index].value);return command>=0&&command<8?names[command]:"Choose a command";}
  if(p->kind==F_NOTE&&index==1){static const char *colors[]={"#FFFF99","#99DDFF","#FFBBDD","#BBEEAA","#FFFFFF"},*names[]={"Yellow","Blue","Pink","Green","White"};for(int i=0;i<5;i++)if(!strcmp(p->fields[index].value,colors[i]))return names[i];}
  return p->fields[index].value;
}
const char *furniture_help_text(void){
  json_t *wired=json_object_get(opened,"wired");const char *message=string(json_object_get(wired,"controls"),"message");
  if(*message)return message;
  return !strcmp(string(json_object_get(wired,"config"),"operation"),"wf_xtra_mov_no_animation")?"Move furniture and players instantly while this box is on the stack.":"";
}
bool furniture_target_switchable(void){return (target_mask&(target_mask-1))!=0;}
bool furniture_player_motion(void){return motion_kind==1||motion_kind==2;}
bool furniture_target_picker(void){return furniture_panel.kind==F_WIRED&&!furniture_panel.variable_definition&&(control_hints?target_mask!=0:!furniture_panel.compact_motion||motion_kind!=0);}
void furniture_toggle_target_kind(void){for(int i=0;i<3;i++){target_kind=target_kind==0?2:target_kind==2?1:0;if(target_mask&(1u<<target_kind))break;}furniture_panel.target=0;}
void furniture_save(void){
  FurniturePanel *p=&furniture_panel;
  if(p->kind==F_MAGIC_TILE){
    if(!p->editable||p->field_count<2)return;
    int height;if(atoi(p->fields[1].value)==1)height=-100;
    else if(!height_hundredths(p->fields[0].value,&height)||height<number(json_object_get(opened,"data"),"minimum")){
      snprintf(p->status,sizeof p->status,"Enter a height in the shown range with up to two decimal places.");return;
    }
    send(json_pack("{s:s,s:I,s:i}","op","magic_tile_save","item",(json_int_t)p->item,"height",height));snprintf(p->status,sizeof p->status,"Saving...");return;
  }
  if(p->kind==F_GIFT_BUY){
    bool restricted=cat_offers[gift_wrapper].requires_club;
    for(int i=0;i<cat_offer_count;i++)if(cat_offers[i].id==gift_offer)restricted|=cat_offers[i].requires_club;
    if(restricted&&!world_club_member()){snprintf(p->status,sizeof p->status,"This item requires Tweebbo Club membership.");return;}
    if(!p->fields[0].value[0]){snprintf(p->status,sizeof p->status,"Enter the recipient's name.");return;}
    send(json_pack("{s:s,s:i,s:i,s:s,s:s,s:s}","op","gift_buy","offer",(int)gift_offer,"wrapper",(int)cat_offers[gift_wrapper].id,"recipient",p->fields[0].value,"message",p->fields[1].value,"requestKey",gift_key));return;
  }
  const char *op=p->kind==F_NOTE?"note_save":p->kind==F_TROPHY?"trophy_inscribe":p->kind==F_MOODLIGHT?"moodlight_save":p->kind==F_MANNEQUIN||p->kind==F_CHANGING||p->kind==F_FOOTBALL_GATE?"outfit_save":p->kind==F_WIRED?"wired_save":p->kind==F_CLOCK?"clock_control":NULL;
  if(!op)return;
  if(p->kind==F_TROPHY?!p->fields[0].editable:!p->editable)return;
  json_t *o=json_pack("{s:s,s:I}","op",op,"item",(json_int_t)p->item);
  if(p->kind==F_WIRED){
    json_t *ops=json_object_get(json_object_get(opened,"wired"),"operations");
    for(size_t i=0;i<json_array_size(ops);i++){json_t *op=json_array_get(ops,i);if(!strcmp(string(op,"name"),p->fields[0].value)&&json_is_false(json_object_get(op,"supported"))){snprintf(p->status,sizeof p->status,"This behavior is unavailable in this version.");json_decref(o);return;}}
    json_t *config=json_deep_copy(json_object_get(json_object_get(opened,"wired"),"config"));
    json_t *params=json_deep_copy(json_object_get(config,"params"));if(!json_is_array(params)){json_decref(params);params=json_array();}
    json_object_set_new(config,"operation",json_string(p->fields[0].value));json_object_set_new(config,"text",json_string(p->fields[1].value));json_object_set_new(config,"delay",json_integer(atoi(p->fields[2].value)));
    for(int i=3;i<3+wired_params&&i<p->field_count;i++){json_t *value=json_integer(atoll(p->fields[i].value));if((size_t)(i-3)<json_array_size(params))json_array_set_new(params,(size_t)(i-3),value);else json_array_append_new(params,value);}
    json_t *extra=json_deep_copy(json_object_get(config,"extra"));if(!json_is_object(extra)){json_decref(extra);extra=json_object();}
    if(wired_rows){json_t *rows=json_array();for(int row=0;row<wired_rows;row++){int at=3+wired_params+row*2;if(at+1>=p->field_count){json_decref(rows);json_decref(extra);json_decref(params);json_decref(config);json_decref(o);return;}json_array_append_new(rows,json_pack("{s:s,s:s}","key",p->fields[at].value,"text",p->fields[at+1].value));}json_object_set_new(extra,"rows",rows);}
    if(json_is_array(json_object_get(json_object_get(opened,"wired"),"textFields"))){json_t *parts=json_array();for(int part=0;part<wired_text_parts;part++){int at=3+wired_params+wired_rows*2+part;if(at>=p->field_count){json_decref(parts);json_decref(extra);json_decref(params);json_decref(config);json_decref(o);return;}json_array_append_new(parts,json_string(p->fields[at].value));}json_object_set_new(extra,"textFields",parts);}
    if(json_object_size(extra))json_object_set_new(config,"extra",extra);else json_decref(extra);
    json_object_set_new(config,"params",params);json_object_set_new(o,"config",config);
  }else for(int i=0;i<p->field_count;i++){
    FurnitureField *f=&p->fields[i];if(!f->editable)continue;
    bool numeric_field=(p->kind==F_MOODLIGHT&&i>0)||p->kind==F_CLOCK;
    json_object_set_new(o,f->key,numeric_field?(!strcmp(f->key,"enabled")||!strcmp(f->key,"backgroundOnly"))?json_boolean(atoi(f->value)!=0):json_integer(atoll(f->value)):json_string(f->value));
  }
  if(send(o))snprintf(p->status,sizeof p->status,"Saving...");
}
void furniture_apply(void){FurniturePanel *p=&furniture_panel;if(!p->can_apply)return;const char *op=p->kind==F_GIFT?"gift_open":"outfit_apply";send(json_pack("{s:s,s:I}","op",op,"item",(json_int_t)p->item));}
const char *furniture_arena_text(void){
  static char out[192];out[0]=0;const char *kind=string(arena,"kind");if(!*kind)return out;
  json_t *scores=json_object_get(arena,"scores"),*players=json_object_get(arena,"players");char key[24];snprintf(key,sizeof key,"%u",W.me);json_t *me=json_object_get(players,key);
  snprintf(out,sizeof out,"%s %ds | B:%d R:%d Y:%d G:%d",!strcmp(kind,"freeze")?"Freeze":"Battle Banzai",number(arena,"remaining")/1000,(int)json_integer_value(json_array_get(scores,0)),(int)json_integer_value(json_array_get(scores,1)),(int)json_integer_value(json_array_get(scores,2)),(int)json_integer_value(json_array_get(scores,3)));
  if(!strcmp(kind,"freeze")){size_t n=strlen(out);if(json_is_object(me))snprintf(out+n,sizeof out-n," | Lives:%d%s",number(me,"lives"),json_is_true(json_object_get(me,"frozen"))?" Frozen":json_is_true(json_object_get(me,"shielded"))?" Shield":"");else snprintf(out+n,sizeof out-n," | Spectating");}return out;
}
bool furniture_can_throw(void){
  char key[24];snprintf(key,sizeof key,"%u",W.me);json_t *me=json_object_get(json_object_get(arena,"players"),key);
  return !strcmp(string(arena,"kind"),"freeze")&&json_is_true(json_object_get(arena,"running"))&&number(me,"lives")>0&&!json_is_true(json_object_get(me,"frozen"));
}
void furniture_throw(void){
  if(!furniture_can_throw())return;
  Ent *me=world_ent(W.me);if(!me)return;Item *target=NULL;int best=100;
  for(int i=0;i<MAX_ITEMS;i++)if(W.items[i].used){const FurniDef *d=def_get(W.items[i].def);int dx=abs((int)W.items[i].x-me->x),dy=abs((int)W.items[i].y-me->y);if(!d||d->arena_role!=1||W.items[i].state||dx>4||dy>4)continue;int distance=dx+dy;if(distance&&distance<best){best=distance;target=&W.items[i];}}
  if(target)send(json_pack("{s:s,s:s,s:I}","op","arena_throw","kind","freeze","item",(json_int_t)target->id));
}
void furniture_gift(u16 offer){
  static u32 serial;const CatOffer *chosen=NULL;
  for(int i=0;i<cat_offer_count;i++)if(cat_offers[i].id==offer)chosen=&cat_offers[i];
  if(!chosen||!chosen->available||(chosen->requires_club&&!world_club_member()))return;
  int wrapper=-1;
  for(int i=0;i<cat_offer_count;i++){const FurniDef *d=def_get(cat_offers[i].def);if(cat_offers[i].available&&(!cat_offers[i].requires_club||world_club_member())&&d&&d->feature==F_GIFT){wrapper=i;break;}}
  if(wrapper<0)return;
  memset(&furniture_panel,0,sizeof furniture_panel);FurniturePanel *p=&furniture_panel;p->revision=++open_revision;p->kind=F_GIFT_BUY;p->pending=true;p->editable=true;p->definition=chosen->def;
  snprintf(p->name,sizeof p->name,"Send as gift");field("recipient","Recipient name","",32,true);field("message","Gift message","",500,true);field("wrapper","Gift wrapping",cat_offers[wrapper].name,80,true);
  gift_offer=offer;gift_wrapper=wrapper;snprintf(gift_key,sizeof gift_key,"native-%llu-%u-%u-%08x",(unsigned long long)now_ms(),(unsigned)offer,(unsigned)++serial,(unsigned)rand());
  gift_total();
}
static void gift_total(void){
  for(int i=0;i<cat_offer_count;i++)if(cat_offers[i].id==gift_offer){snprintf(furniture_panel.status,sizeof furniture_panel.status,"Total: %u credits (wrapping: %u).",(unsigned)(cat_offers[i].price+cat_offers[gift_wrapper].price),(unsigned)cat_offers[gift_wrapper].price);break;}
}
const char *furniture_operation_label(void){
  json_t *ops=json_object_get(json_object_get(opened,"wired"),"operations");const char *name=furniture_panel.fields[0].value;
  for(size_t i=0;i<json_array_size(ops);i++){json_t *op=json_array_get(ops,i);const char *label=string(op,"label");if(!strcmp(string(op,"name"),name)&&*label&&strncmp(label,"wf_",3))return label;}
  /* Never show internal behavior ids such as wf_cnd_match_time: fall back to the box's catalogue name. */
  const FurniDef *d=def_get(furniture_panel.definition);
  if(d&&d->name&&*d->name&&strncmp(d->name,"wf_",3))return d->name;
  if(*furniture_panel.name&&strncmp(furniture_panel.name,"wf_",3))return furniture_panel.name;
  return "Wired box";
}

u32 furniture_item_color(u32 item){const Appearance *a=find_appearance(item);return a?a->color:0xffffff;}
bool furniture_item_look(u32 item,u64 *look){const Appearance *a=find_appearance(item);for(;a&&a<appearances+appearance_count&&a->id==item;a++)if(a->has_look){*look=a->look;return true;}return false;}
