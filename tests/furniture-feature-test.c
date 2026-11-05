#include "furniture.h"
#include "catalog.h"
#include <jansson.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static json_t *sent;
static unsigned sent_count;
const CatOffer cat_offers[]={{.id=1,.def=1,.price=5,.available=1,.name="Chair"},{.id=2,.def=2,.price=2,.available=1,.name="Gift Box"},
  {.id=3,.def=1,.price=9,.available=1,.requires_club=1,.name="Club Chair"},{.id=4,.def=2,.price=3,.available=1,.requires_club=1,.name="Club Gift Box"}};
const int cat_offer_count=4;
static bool member;
bool world_club_member(void){return member;}
static const FurniDef fixtures[]={{.id=1},{.id=2,.feature=F_GIFT}};
const FurniDef *def_get(u16 id){return id>=1&&id<=2?&fixtures[id-1]:NULL;}
u64 now_ms(void){return 100000;}
void send_furniture(const char *text){json_decref(sent);sent=json_loads(text,0,NULL);assert(sent);sent_count++;}

int main(void){
  json_error_t fixture_error;
  json_t *variable_editors=json_load_file("hotel/tests/fixtures/wired-variable-editors.json",JSON_REJECT_DUPLICATES,&fixture_error);
  if(!variable_editors)fprintf(stderr,"Variable editor fixture: line %d, column %d: %s\n",fixture_error.line,fixture_error.column,fixture_error.text);
  assert(variable_editors);
  const char *editor_names[]={"user","room","context","give","change","condition","referencedGive","referencedChange","referencedCondition","referencedAge","sameHolderChange","userSelector","furniSelector","sameHolderSelector","animationTime","instantMotion","echo","reference","moveAvatar","carryUsers","physics","furniToFurni","furniToUser","groupMovement","moveToOffset","headingMovement","chaseMovement","fleeMovement","collisionTrigger"};
  for(unsigned i=0;i<sizeof editor_names/sizeof editor_names[0];i++){
    json_t *editor=json_object_get(variable_editors,editor_names[i]);assert(editor);
    char *raw=json_dumps(editor,JSON_COMPACT);assert(raw);furniture_receive(raw);free(raw);
    assert(furniture_panel.kind==F_WIRED&&furniture_panel.editable);
    if(i<3||(i>=16&&i<18)){
      assert(furniture_panel.variable_definition&&furniture_display_index(0)==1);
      assert(furniture_display_count()==(i==0||i==17?3:2));assert(furniture_display_index(furniture_display_count())==-1);
      furniture_panel.selected=1;assert(!furniture_edit("invalid name"));assert(!furniture_edit("12345678901234567890123456789012345678901"));
    }else if(i==14||i==15){
      assert(furniture_panel.compact_motion&&furniture_display_count()==(i==14?1:0));
      if(i==14){furniture_panel.selected=3;assert(!furniture_edit("49"));assert(!furniture_edit("2001"));}
    }else if(i==18||i==19){
      assert(furniture_panel.compact_motion&&furniture_player_motion());
      assert(furniture_display_count()==(i==18?4:2));assert(furniture_display_index(0)==(i==18?2:3));
      assert(!strcmp(furniture_target_kind(),"Players"));furniture_toggle_target_kind();assert(!strcmp(furniture_target_kind(),"Players"));
    }else if(i==20){assert(furniture_panel.compact_motion&&furniture_target_picker()&&!furniture_player_motion());assert(furniture_display_count()==7&&furniture_display_index(0)==3);}else if(i>=21&&i<=24){assert(furniture_panel.compact_motion&&furniture_target_picker());assert(furniture_display_index(0)==2);assert(furniture_display_count()==(i==23?8:i==24?5:3));}else if(i>=25){assert(furniture_panel.compact_motion&&!furniture_target_switchable());assert(furniture_display_count()==(i==25?5:i==28?0:2));assert(furniture_target_picker()==(i!=28));if(i==28)assert(strstr(furniture_help_text(),"collision")&&!strstr(furniture_help_text(),"instantly"));}else assert(!furniture_panel.variable_definition&&furniture_display_count()==furniture_panel.field_count);
    json_t *settings=json_object_get(json_object_get(editor,"wired"),"config");
    furniture_save();assert(!strcmp(json_string_value(json_object_get(json_object_get(sent,"config"),"operation")),json_string_value(json_object_get(settings,"operation"))));
    json_t *saved_params=json_object_get(json_object_get(sent,"config"),"params"),*original_params=json_object_get(settings,"params"),*parameters=json_object_get(json_object_get(editor,"wired"),"parameters");
    size_t count=json_array_size(parameters)>json_array_size(original_params)?json_array_size(parameters):json_array_size(original_params);assert(json_array_size(saved_params)==count);
    for(size_t j=0;j<count;j++){
      json_t *expected=json_array_get(original_params,j);if(!expected)expected=json_object_get(json_array_get(parameters,j),"default");
      assert(json_integer_value(json_array_get(saved_params,j))==json_integer_value(expected));
    }
    if(i>=16&&i<18){
      assert(furniture_has_choices(3));assert(strstr(furniture_choice_label(3),i==16?"remote_total":"shared_total"));
      if(i==17){
        assert(json_integer_value(json_array_get(saved_params,0))>(1LL<<34));
        assert(!strcmp(json_string_value(json_object_get(json_object_get(sent,"config"),"variable_room")),"shared-home"));
        furniture_choice(4,1);furniture_save();assert(json_integer_value(json_array_get(json_object_get(json_object_get(sent,"config"),"params"),1))==1);
      }
    }
    if(i==18){
      assert(furniture_has_choices(3)&&furniture_has_choices(4)&&furniture_has_choices(5));
      assert(!strcmp(furniture_choice_label(3),"East"));assert(!strcmp(furniture_choice_label(4),"Clockwise"));
      furniture_choice(4,1);furniture_save();assert(json_integer_value(json_array_get(json_object_get(json_object_get(sent,"config"),"params"),1))==9);
      furniture_panel.selected=3;assert(!furniture_edit("-2"));
    }
    if(i==19){
      assert(!strcmp(furniture_choice_label(4),"All players"));furniture_choice(4,1);furniture_save();
      assert(json_integer_value(json_array_get(json_object_get(json_object_get(sent,"config"),"params"),1))==0);
      furniture_panel.selected=4;assert(!furniture_edit("999"));
    }
    if(i==20){
      for(int j=3;j<10;j++)assert(furniture_has_choices(j));
      assert(!strcmp(furniture_target_kind(),"Primary"));
      size_t primary_count=json_array_size(json_object_get(settings,"targets")),secondary_count=json_array_size(json_object_get(settings,"secondary_targets"));
      bool primary_selected=furniture_target_check()==1;furniture_target(0,true);furniture_save();
      json_t *config=json_object_get(sent,"config");assert(json_array_size(json_object_get(config,"targets"))==primary_count+(primary_selected?-1:1));
      assert(json_equal(json_object_get(config,"secondary_targets"),json_object_get(settings,"secondary_targets")));
      furniture_toggle_target_kind();assert(!strcmp(furniture_target_kind(),"Secondary"));
      bool secondary_selected=furniture_target_check()==1;furniture_target(0,true);furniture_save();config=json_object_get(sent,"config");
      assert(json_array_size(json_object_get(config,"targets"))==primary_count+(primary_selected?-1:1));
      assert(json_array_size(json_object_get(config,"secondary_targets"))==secondary_count+(secondary_selected?-1:1));
      assert(json_equal(json_object_get(config,"users"),json_object_get(settings,"users")));
      furniture_toggle_target_kind();assert(!strcmp(furniture_target_kind(),"Players"));furniture_target(0,true);furniture_save();config=json_object_get(sent,"config");
      assert(!json_equal(json_object_get(config,"users"),json_object_get(settings,"users")));
      assert(json_array_size(json_object_get(config,"secondary_targets"))==secondary_count+(secondary_selected?-1:1));
      furniture_toggle_target_kind();assert(!strcmp(furniture_target_kind(),"Primary"));
      furniture_panel.selected=3;assert(!furniture_edit("2"));furniture_panel.selected=8;assert(!furniture_edit("999"));
    }
    if(i>=21&&i<=24){
      assert(!strcmp(furniture_target_kind(),"Primary"));assert(!furniture_player_motion());
      furniture_toggle_target_kind();assert(!strcmp(furniture_target_kind(),i==22?"Players":"Secondary"));
      furniture_target(0,true);furniture_save();json_t *config=json_object_get(sent,"config");
      assert(json_equal(json_object_get(config,"targets"),json_object_get(settings,"targets")));
      const char *scope=i==22?"users":"secondary_targets";assert(!json_equal(json_object_get(config,scope),json_object_get(settings,scope)));
      furniture_toggle_target_kind();assert(!strcmp(furniture_target_kind(),i==23?"Players":"Primary"));
      if(i==23){furniture_toggle_target_kind();assert(!strcmp(furniture_target_kind(),"Primary"));}
      furniture_panel.selected=3;assert(furniture_has_choices(3));assert(!furniture_edit("999"));
    }
    if(i>=25&&i<=27){
      assert(!strcmp(furniture_target_kind(),"Primary"));furniture_toggle_target_kind();assert(!strcmp(furniture_target_kind(),"Primary"));
      assert(furniture_display_index(0)==2);for(int j=3;j<furniture_panel.field_count;j++)assert(furniture_has_choices(j));
      furniture_choice(i==25?4:3,1);furniture_save();json_t *config=json_object_get(sent,"config");
      assert(json_equal(json_object_get(config,"targets"),json_object_get(settings,"targets")));
      assert(json_integer_value(json_array_get(json_object_get(config,"params"),i==25?1:0))==(i==25?3:200));
    }
    if(i>=3&&i<11){
      assert(furniture_has_choices(5));assert(strstr(furniture_choice_label(5),"personal_total"));
      long long original=json_integer_value(json_array_get(json_object_get(settings,"params"),2));
      furniture_choice(5,1);assert(strstr(furniture_choice_label(5),"room_total"));furniture_save();
      assert(json_integer_value(json_array_get(json_object_get(json_object_get(sent,"config"),"params"),2))!=original);
      furniture_choice(5,-1);assert(strstr(furniture_choice_label(5),"personal_total"));
      furniture_panel.selected=5;assert(!furniture_edit("4294967295"));
    }
  }
  printf("Actual server forms: %zu; movement scopes, named choices and exact values passed.\n",sizeof editor_names/sizeof editor_names[0]);
  json_decref(variable_editors);
  json_t *config=json_pack("{s:s,s:s,s:i,s:[],s:[],s:[]}","operation","wf_cnd_match_time","text","UTC","delay",0,"targets","users","params");
  json_t *params=json_object_get(config,"params"),*schema=json_array();
  for(int i=0;i<32;i++){json_array_append_new(params,json_integer(i));json_array_append_new(schema,json_pack("{s:s,s:i}","label","A named setting","default",0));}
  json_t *wired=json_pack("{s:b,s:o,s:o,s:o,s:o,s:o}","supported",1,"config",config,"parameters",schema,
    "operations",json_pack("[{s:s,s:s,s:b}]","name","wf_cnd_match_time","label","Time matches","supported",1),
    "items",json_pack("[{s:i,s:s}]","id",88,"name","Lamp"),"users",json_pack("[{s:i,s:s}]","id",77,"name","Player"));
  json_t *opened=json_pack("{s:s,s:s,s:s,s:i,s:b,s:b,s:{},s:o}","op","open","kind","wired","name","Wired","item",42,"editable",1,"owned",1,"data","wired",wired);
  assert(opened);char *raw=json_dumps(opened,JSON_COMPACT);furniture_receive(raw);free(raw);json_decref(opened);
  assert(furniture_panel.field_count==35);
  assert(!furniture_panel.fields[0].editable);
  furniture_panel.selected=34;assert(furniture_edit("9223372036854775807"));
  assert(furniture_edit("-9223372036854775808"));assert(furniture_edit("9223372036854775807"));
  assert(!furniture_edit("9223372036854775808"));assert(!furniture_edit("not a number"));
  furniture_target(0,true);assert(furniture_target_check()==1&&!strcmp(furniture_target_label(),"Lamp"));
  furniture_toggle_target_kind();furniture_target(0,true);assert(furniture_target_check()==1&&!strcmp(furniture_target_label(),"Player"));
  furniture_save();json_t *saved=json_object_get(sent,"config");
  assert(json_array_size(json_object_get(saved,"params"))==32);
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),31))==9223372036854775807LL);
  assert(json_integer_value(json_array_get(json_object_get(saved,"targets"),0))==88);
  assert(json_integer_value(json_array_get(json_object_get(saved,"users"),0))==77);
  assert(!strcmp(json_string_value(json_object_get(saved,"operation")),"wf_cnd_match_time"));
  config=json_pack("{s:s,s:s,s:i,s:[],s:[],s:[],s:{s:b}}","operation","wf_act_send_dataframe","text","[]","delay",0,"targets","users","params","extra","retained",1);
  params=json_object_get(config,"params");schema=json_array();
  for(int i=0;i<74;i++){if(i<37)json_array_append_new(params,json_integer(i?0:12));json_array_append_new(schema,json_pack("{s:s,s:i}","label","Dataframe setting","default",i<37?0:i));}
  json_t *rows=json_array();for(int i=0;i<12;i++){char key[16];snprintf(key,sizeof key,"key_%d",i);json_array_append_new(rows,json_pack("{s:s,s:s,s:s,s:s}","key",key,"text","A row value","keyLabel","Row key","textLabel","Row text"));}
  json_object_set_new(json_array_get(schema,48),"picker",json_string("variables"));
  wired=json_pack("{s:b,s:o,s:o,s:o,s:o,s:[],s:[]}","supported",1,"config",config,"parameters",schema,"textRows",rows,"variableChoices",json_pack("[{s:I,s:s}]","value",(json_int_t)-459,"label","Changed variable box (Execution)"),"items","users");
  opened=json_pack("{s:s,s:s,s:s,s:i,s:b,s:b,s:{},s:o}","op","open","kind","wired","name","Send dataframe","item",91,"editable",1,"owned",1,"data","wired",wired);
  raw=json_dumps(opened,JSON_COMPACT);assert(raw);furniture_receive(raw);free(raw);json_decref(opened);
  assert(furniture_panel.editable&&furniture_panel.field_count==101&&furniture_panel.values_used<=sizeof furniture_panel.values);
  assert(furniture_has_choices(51));furniture_choice(51,1);assert(!strcmp(furniture_choice_label(51),"Changed variable box (Execution)"));
  furniture_panel.selected=100;assert(furniture_edit("The final row is preserved"));furniture_save();saved=json_object_get(sent,"config");
  assert(json_array_size(json_object_get(saved,"params"))==74);assert(json_integer_value(json_array_get(json_object_get(saved,"params"),48))==-459);
  assert(json_is_true(json_object_get(json_object_get(saved,"extra"),"retained")));rows=json_object_get(json_object_get(saved,"extra"),"rows");assert(json_array_size(rows)==12);assert(!strcmp(json_string_value(json_object_get(json_array_get(rows,11),"text")),"The final row is preserved"));
  furniture_panel.selected=3;assert(furniture_edit("1"));assert(furniture_display_count()<30);
  furniture_receive("{\"op\":\"open\",\"kind\":\"wired\",\"item\":92,\"name\":\"Bot outfit\",\"look\":1234,\"editable\":true,\"data\":{},\"wired\":{\"supported\":true,\"config\":{\"operation\":\"wf_act_bot_clothes\",\"text\":\"Bot\",\"delay\":0,\"targets\":[],\"users\":[],\"params\":[100,0]},\"parameters\":[{\"label\":\"Bot source\"},{\"label\":\"Outfit\",\"action\":\"currentOutfit\"}],\"textFields\":[{\"label\":\"Bot name\",\"value\":\"Bot\",\"limit\":64}],\"items\":[],\"users\":[]}}");
  assert(furniture_has_choices(4));furniture_choice(4,1);assert(!strcmp(furniture_choice_label(4),"Current outfit selected"));furniture_panel.selected=5;assert(furniture_edit("New bot name"));furniture_save();saved=json_object_get(sent,"config");assert(json_integer_value(json_array_get(json_object_get(saved,"params"),1))==1234);assert(!strcmp(json_string_value(json_array_get(json_object_get(json_object_get(saved,"extra"),"textFields"),0)),"New bot name"));
  printf("Modern Wired editor: all 101 dataframe fields, 12 key/text rows, shared signed reference choices, pool budget, conditional rows, structured text and current outfit passed.\n");
  furniture_receive("{\"op\":\"open\",\"kind\":\"wired\",\"item\":45,\"name\":\"Relative furniture movement\",\"editable\":true,\"data\":{},\"wired\":{\"supported\":true,\"config\":{\"operation\":\"wf_act_rel_mov\",\"text\":\"\",\"delay\":0,\"targets\":[93,88],\"users\":[],\"params\":[1,0]},\"parameters\":[{\"label\":\"Horizontal tile offset\"},{\"label\":\"Vertical tile offset\"}],\"items\":[{\"id\":88,\"name\":\"First mover\"},{\"id\":93,\"name\":\"Second mover\"}],\"users\":[]}}");
  furniture_save();saved=json_object_get(sent,"config");
  assert(json_integer_value(json_array_get(json_object_get(saved,"targets"),0))==93);
  assert(json_integer_value(json_array_get(json_object_get(saved,"targets"),1))==88);
  furniture_target(1,false);furniture_target(0,true);furniture_target(0,true);furniture_save();saved=json_object_get(sent,"config");
  assert(json_integer_value(json_array_get(json_object_get(saved,"targets"),0))==88);
  assert(json_integer_value(json_array_get(json_object_get(saved,"targets"),1))==93);

  furniture_receive("{\"op\":\"open\",\"kind\":\"wired\",\"item\":45,\"name\":\"Give score to team\",\"editable\":true,\"owned\":true,\"data\":{},\"wired\":{\"supported\":true,\"config\":{\"operation\":\"wf_act_give_score_tm\",\"text\":\"\",\"delay\":0,\"targets\":[],\"users\":[],\"params\":[17,1,2,201,3]},\"parameters\":[{\"label\":\"Points\"},{\"label\":\"Add or subtract\"},{\"label\":\"Team\"},{\"label\":\"Player source\"},{\"label\":\"Per-game quota\"}],\"items\":[],\"users\":[]}}");
  assert(furniture_panel.field_count==8);furniture_panel.selected=7;assert(furniture_edit("10"));furniture_save();
  saved=json_object_get(sent,"config");assert(json_array_size(json_object_get(saved,"params"))==5);
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),0))==17);
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),3))==201);
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),4))==10);
  furniture_receive("{\"op\":\"open\",\"kind\":\"wired\",\"item\":46,\"name\":\"Freeze player\",\"editable\":true,\"data\":{},\"wired\":{\"supported\":true,\"config\":{\"operation\":\"wf_act_freeze\",\"text\":\"\",\"delay\":0,\"targets\":[],\"users\":[],\"params\":[11,1,201]},\"parameters\":[{\"label\":\"Effect\"},{\"label\":\"Cancel on teleport\"},{\"label\":\"Player source\"}],\"items\":[],\"users\":[]}}");
  assert(furniture_panel.field_count==6);furniture_save();saved=json_object_get(sent,"config");
  assert(json_array_size(json_object_get(saved,"params"))==3);
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),0))==11);
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),1))==1);
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),2))==201);
  furniture_receive("{\"op\":\"open\",\"kind\":\"wired\",\"item\":47,\"name\":\"Change variable\",\"editable\":true,\"data\":{},\"wired\":{\"supported\":true,\"config\":{\"operation\":\"wf_act_change_var_val\",\"text\":\"counter\",\"delay\":0,\"targets\":[],\"users\":[],\"params\":[9223372036854775807,1]},\"parameters\":[{\"label\":\"Operand\"},{\"label\":\"Operation\",\"choices\":[{\"value\":0,\"label\":\"Set\"},{\"value\":1,\"label\":\"Add\"},{\"value\":112,\"label\":\"Find set bit\"}]}],\"items\":[],\"users\":[]}}");
  assert(furniture_panel.field_count==5&&furniture_has_choices(4)&&!furniture_has_choices(3));
  assert(!strcmp(furniture_choice_label(4),"Add"));furniture_choice(4,-1);assert(!strcmp(furniture_choice_label(4),"Set"));
  furniture_choice(4,-1);assert(!strcmp(furniture_choice_label(4),"Find set bit"));furniture_choice(4,1);assert(!strcmp(furniture_choice_label(4),"Set"));
  furniture_panel.selected=4;assert(!furniture_edit("99"));furniture_choice(4,-1);furniture_save();saved=json_object_get(sent,"config");
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),0))==9223372036854775807LL);
  assert(json_integer_value(json_array_get(json_object_get(saved,"params"),1))==112);
  furniture_panel.fields[4].editable=false;furniture_choice(4,1);assert(!strcmp(furniture_choice_label(4),"Find set bit"));
  furniture_receive("{\"op\":\"open\",\"kind\":\"note\",\"item\":43,\"name\":\"Note\",\"editable\":true,\"owned\":true,\"data\":{}}");
  char note[1001];memset(note,'a',1000);note[1000]=0;furniture_panel.selected=0;assert(furniture_edit(note));furniture_save();assert(strlen(json_string_value(json_object_get(sent,"text")))==1000);
  char max_note[4001];memset(max_note,'n',4000);max_note[4000]=0;assert(furniture_edit(max_note));furniture_save();
  assert(strlen(json_string_value(json_object_get(sent,"text")))==4000);assert(furniture_panel.values_used<=sizeof furniture_panel.values);
  printf("Native furniture editor: %zu bytes, shared value pool %zu bytes; 4000-byte text and all 32 exact integer fields pass.\n",sizeof furniture_panel,sizeof furniture_panel.values);
  furniture_choice(1,1);assert(!strcmp(furniture_choice_label(1),"Blue"));furniture_save();assert(!strcmp(json_string_value(json_object_get(sent,"color")),"#99DDFF"));
  furniture_receive("{\"op\":\"state\",\"light\":{\"enabled\":true,\"color\":\"#804020\",\"intensity\":128,\"backgroundOnly\":true}}");
  assert(furniture_light_alpha()==127&&furniture_light_color()==0x402010&&furniture_light_background());
  furniture_receive("{\"op\":\"open\",\"kind\":\"wired\",\"item\":44,\"name\":\"Pending\",\"editable\":true,\"data\":{},\"wired\":{\"supported\":false,\"config\":{\"operation\":\"pending\",\"targets\":[],\"users\":[],\"params\":[]},\"operations\":[{\"name\":\"pending\",\"supported\":false}]}}");
  unsigned before=sent_count;furniture_save();assert(sent_count==before);
  furniture_gift(1);assert(strstr(furniture_panel.status,"7 credits"));
  u32 gift_revision=furniture_panel.revision;furniture_gift(3);assert(furniture_panel.revision==gift_revision);
  furniture_choice(2,1);assert(!strcmp(furniture_panel.fields[2].value,"Gift Box"));
  member=true;furniture_gift(3);assert(furniture_panel.kind==F_GIFT_BUY&&strstr(furniture_panel.status,"11 credits"));
  furniture_panel.selected=0;assert(furniture_edit("Gift recipient"));member=false;
  before=sent_count;furniture_save();assert(sent_count==before&&strstr(furniture_panel.status,"Club membership"));
  member=true;furniture_save();assert(sent_count==before+1&&json_integer_value(json_object_get(sent,"offer"))==3);
  furniture_gift(1);furniture_choice(2,1);assert(!strcmp(furniture_panel.fields[2].value,"Club Gift Box"));
  furniture_panel.selected=0;assert(furniture_edit("Gift recipient"));member=false;before=sent_count;
  furniture_save();assert(sent_count==before&&strstr(furniture_panel.status,"Club membership"));
  puts("Native Club gifts: product/wrapper restrictions, eligible wrapping choices and membership expiry before submission passed.");
  furniture_gift(1);
  furniture_panel.selected=0;assert(furniture_edit("Recipient"));furniture_save();
  assert(!strcmp(json_string_value(json_object_get(sent,"recipient")),"Recipient"));
  furniture_reset();assert(!furniture_panel.kind&&furniture_light_alpha()==0);
  furniture_receive("{\"op\":\"open\",\"kind\":\"football_gate\",\"item\":40062,\"name\":\"Football Gate\",\"editable\":true,\"owned\":true,\"data\":{\"name\":\"Home kit\",\"gender\":\"Female\",\"look\":1234}}");
  assert(furniture_panel.kind==F_FOOTBALL_GATE&&furniture_panel.field_count==2);
  assert(!strcmp(furniture_panel.fields[1].value,"Female")&&!furniture_panel.fields[1].editable&&!furniture_panel.can_apply);
  furniture_panel.selected=0;assert(furniture_edit("Away kit"));furniture_save();
  assert(!strcmp(json_string_value(json_object_get(sent,"op")),"outfit_save"));
  assert(!strcmp(json_string_value(json_object_get(sent,"name")),"Away kit"));
  before=sent_count;furniture_apply();assert(sent_count==before);
  furniture_receive("{\"op\":\"open\",\"kind\":\"football_gate\",\"item\":40062,\"name\":\"Football Gate\",\"editable\":false,\"data\":{\"gender\":\"Male\"}}");
  before=sent_count;furniture_save();assert(sent_count==before);
  furniture_open(4294967294U);assert(json_integer_value(json_object_get(sent,"item"))==4294967294LL);
  furniture_receive("{\"op\":\"clocks\",\"items\":[{\"id\":4294967294,\"text\":\"1:01\",\"running\":true}]}");
  assert(!strcmp(furniture_clock_text(4294967294U),"1:01"));assert(!furniture_clock_text(99));
  furniture_receive("{\"op\":\"clocks\",\"items\":[{\"id\":40023,\"text\":\"13\",\"running\":false},{\"id\":40049,\"text\":\"4294967295\",\"running\":false}]}");
  assert(!strcmp(furniture_clock_text(40023),"13")&&!strcmp(furniture_clock_text(40049),"4294967295"));
  assert(!furniture_clock_text(4294967294U));
  furniture_receive("{\"op\":\"open\",\"kind\":\"clock\",\"item\":4294967294,\"name\":\"Counter\",\"editable\":true,\"data\":{\"time\":\"1:01\"}}");
  assert(furniture_panel.kind==F_CLOCK&&furniture_panel.field_count==4);
  furniture_choice(1,-1);assert(!strcmp(furniture_choice_label(1),"Set time"));
  furniture_panel.selected=2;assert(!furniture_edit("100"));assert(furniture_edit("1"));
  furniture_panel.selected=3;assert(!furniture_edit("120"));assert(furniture_edit("3"));furniture_save();
  assert(!strcmp(json_string_value(json_object_get(sent,"op")),"clock_control"));
  assert(json_integer_value(json_object_get(sent,"item"))==4294967294LL);
  assert(json_integer_value(json_object_get(sent,"command"))==7&&json_integer_value(json_object_get(sent,"minutes"))==1&&json_integer_value(json_object_get(sent,"steps"))==3);
  furniture_receive("{\"op\":\"open\",\"kind\":\"magic_tile\",\"item\":4294967294,\"name\":\"Stack Magic Tile\",\"placed\":true,\"editable\":true,\"data\":{\"height\":1,\"minimum\":0,\"maximum\":4000}}");
  assert(furniture_panel.kind==F_MAGIC_TILE&&furniture_panel.field_count==2);
  assert(!strcmp(furniture_panel.fields[0].value,"0.01")&&!strcmp(furniture_panel.fields[0].label,"Height (0.00-40.00)"));
  furniture_panel.selected=0;assert(furniture_edit("0.29"));furniture_save();
  assert(!strcmp(json_string_value(json_object_get(sent,"op")),"magic_tile_save"));assert(json_integer_value(json_object_get(sent,"height"))==29);
  assert(json_integer_value(json_object_get(sent,"item"))==4294967294LL);
  assert(!furniture_edit("0.001")&&!furniture_edit("40.01")&&!furniture_edit("-1")&&!furniture_edit("1e1"));
  assert(furniture_has_choices(1));furniture_choice(1,1);assert(!strcmp(furniture_choice_label(1),"Match stack below"));furniture_save();assert(json_integer_value(json_object_get(sent,"height"))==-100);
  furniture_receive("{\"op\":\"saved\",\"kind\":\"magic_tile\",\"item\":4294967294,\"message\":\"Height updated.\",\"data\":{\"height\":128,\"minimum\":50}}");
  assert(!strcmp(furniture_panel.fields[0].value,"1.28")&&!strcmp(furniture_panel.fields[0].label,"Height (0.50-40.00)"));
  assert(!strcmp(furniture_choice_label(1),"Use entered height"));assert(!furniture_edit("0.49"));assert(furniture_edit("0.50"));
  furniture_receive("{\"op\":\"saved\",\"kind\":\"magic_tile\",\"item\":8,\"data\":{\"height\":4000}}");assert(!strcmp(furniture_panel.fields[0].value,"0.50"));
  furniture_receive("{\"op\":\"open\",\"kind\":\"magic_tile\",\"item\":9,\"placed\":false,\"editable\":true,\"data\":{}}");
  assert(!furniture_panel.editable&&!furniture_panel.field_count);unsigned no_save=sent_count;furniture_save();assert(sent_count==no_save);
  furniture_receive("{\"op\":\"open\",\"kind\":\"magic_tile\",\"item\":9,\"placed\":true,\"editable\":false,\"data\":{\"height\":200}}");
  assert(!furniture_panel.fields[0].editable);assert(!furniture_edit("3.00"));furniture_save();assert(sent_count==no_save);
  puts("Magic helper form: exact decimals, Match Below, canonical replies, dynamic floor minimum, stale replies, inventory and room-rights states passed.");
  furniture_receive("{\"op\":\"open\",\"kind\":\"wired\",\"item\":42,\"name\":\"Web API\",\"editable\":true,\"data\":{},\"wired\":{\"supported\":true,\"config\":{\"operation\":\"wf_xtra_var_web_api\",\"text\":\"\\t\",\"targets\":[],\"users\":[],\"params\":[0],\"delay\":0},\"operations\":[{\"name\":\"wf_xtra_var_web_api\",\"supported\":true}],\"parameters\":[{\"label\":\"Allow writes\",\"default\":0}],\"textFields\":[{\"label\":\"Read key\",\"value\":\"\",\"limit\":43,\"editable\":false},{\"label\":\"Write key\",\"value\":\"\",\"limit\":43,\"editable\":false}]}}");
  int read_action=-1,write_action=-1;for(int i=0;i<furniture_panel.field_count;i++){if(!strcmp(furniture_panel.fields[i].key,"apiRead"))read_action=i;if(!strcmp(furniture_panel.fields[i].key,"apiWrite"))write_action=i;}assert(read_action>=0&&write_action>=0);
  furniture_choice(read_action,1);assert(!strcmp(json_string_value(json_object_get(sent,"op")),"wired_web_api_key")&&!json_is_true(json_object_get(sent,"write")));
  furniture_choice(write_action,1);assert(json_is_true(json_object_get(sent,"write")));
  furniture_receive("{\"type\":\"wired_web_api_key\",\"item\":42,\"write\":false,\"key\":\"RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR\"}");furniture_save();json_t *api_texts=json_object_get(json_object_get(json_object_get(sent,"config"),"extra"),"textFields");assert(strlen(json_string_value(json_array_get(api_texts,0)))==43);
  puts("Native API editor: explicit read/write key actions, private canonical reply and save preservation passed.");
  json_t *large=json_pack("{s:s,s:s,s:i,s:s,s:b,s:{},s:{s:b,s:{s:s,s:s,s:[],s:[],s:[i,i]},s:[],s:[],s:[{s:s,s:s,s:i}]}}","op","open","kind","wired","item",43,"name","Level system","editable",1,"data","wired","supported",1,"config","operation","wf_xtra_var_lvlup_system","text","","targets","users","params",0,0,"operations","parameters","textFields","label","Thresholds","value","","limit",8192);
  char large_text[8194];memset(large_text,'1',8192);large_text[8192]=0;
  json_t *long_part=json_array_get(json_object_get(json_object_get(large,"wired"),"textFields"),0);
  json_object_set_new(long_part,"value",json_string(large_text));
  char *large_raw=json_dumps(large,JSON_COMPACT);assert(large_raw);furniture_receive(large_raw);free(large_raw);
  assert(furniture_panel.editable);furniture_panel.selected=5;
  assert(strlen(furniture_panel.fields[5].value)==8192&&furniture_edit(large_text));
  before=sent_count;furniture_save();assert(sent_count==before+1);
  assert(strlen(json_string_value(json_array_get(json_object_get(json_object_get(json_object_get(sent,"config"),"extra"),"textFields"),0)))==8192);
  large_text[8192]='2';large_text[8193]=0;assert(!furniture_edit(large_text));
  json_object_set_new(long_part,"value",json_string(large_text));large_raw=json_dumps(large,JSON_COMPACT);furniture_receive(large_raw);free(large_raw);
  assert(!furniture_panel.editable&&strstr(furniture_panel.status,"too large"));before=sent_count;furniture_save();assert(sent_count==before);
  json_decref(large);
  puts("Native long-text editor: exact 8192-byte edit/save, bounded allocation and oversized protection passed.");
  furniture_reset();assert(!furniture_clock_text(4294967294U));
  json_decref(sent);puts("Furniture protocol: all 32 settings, stable behavior, furniture/player selections, long notes, moodlight, pending guard, gift charge and full u32 identities passed.");
  return 0;
}
