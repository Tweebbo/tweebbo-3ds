#include "furniture.h"
#include "ui_int.h"
#include "osk.h"
#include "language.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

static int page;
static int reading=-1,offsets[64],read_page;
static u32 revision;
static void edited(const char *text,void *ctx){(void)ctx;furniture_edit(text);}
void furniture_draw(u64 now){
  (void)now;FurniturePanel *p=&furniture_panel;
  if(revision!=p->revision){revision=p->revision;reading=-1;read_page=0;page=0;}
  if(sk_window(WIN_X,WIN_Y,WIN_W,WIN_H,TR(p->name))){ui_close();return;}
  if(reading>=0&&reading<p->field_count){
    sk_content(IN_X,IN_Y,IN_W,IN_H);
    FurnitureField *f=&p->fields[reading];
    int next=sk_paragraph_page(f->value,offsets[read_page],IN_X+5,IN_Y+5,IN_W-10,11,SK_TEXT,(IN_H-42)/12);
    if(sk_button(IN_X+4,IN_Y+IN_H-25,56,TR("Back"),true))reading=-1;
    if(sk_button(IN_X+68,IN_Y+IN_H-25,56,TR("Prev"),read_page>0))read_page--;
    if(sk_button(IN_X+IN_W-60,IN_Y+IN_H-25,56,TR("Next"),next>offsets[read_page]&&f->value[next]&&read_page<63)){offsets[++read_page]=next;}
    return;
  }
  sk_content(IN_X,IN_Y,IN_W,IN_H);int pages=(furniture_display_count()+1)/2;if(pages<1)pages=1;if(page>=pages)page=pages-1;
  if((K.down&KEY_L)&&page>0)page--;
  if((K.down&KEY_R)&&page+1<pages)page++;
  for(int row=0;row<2;row++){
    int i=furniture_display_index(page*2+row);if(i<0)break;FurnitureField *f=&p->fields[i];float y=IN_Y+3+row*32;
    sk_text(TR(f->label),IN_X+4,y,10,SK_TEXT,0,IN_W-8);
    bool choice=furniture_has_choices(i);
    if(p->kind==F_WIRED&&i==0){
      sk_text(furniture_operation_label(),IN_X+4,y+16,9,SK_TEXT,0,IN_W-8);
    }else if(choice){
      if(sk_button(IN_X+4,y+12,24,"<",p->editable))furniture_choice(i,-1);
      sk_text(TR(furniture_choice_label(i)),IN_X+34,y+16,9,SK_TEXT,0,IN_W-68);
      if(sk_button(IN_X+IN_W-28,y+12,24,">",p->editable))furniture_choice(i,1);
    }else{
      bool long_text=strlen(f->value)>48;
      if(sk_field(IN_X+4,y+12,IN_W-8-(long_text?52:0),f->value,TR(f->label))&&f->editable){p->selected=i;osk_open(TR(f->label),f->value,f->limit,edited,NULL);}
      if(long_text&&sk_button(IN_X+IN_W-52,y+12,48,TR("Read"),true)){reading=i;read_page=0;offsets[0]=0;}
    }
  }
  float y=IN_Y+69;
  if(p->compact_motion&&!furniture_display_count())sk_paragraph_page(TR(furniture_help_text()),0,IN_X+4,IN_Y+4,IN_W-8,10,SK_TEXT,3);
  if(furniture_target_picker()){
    if(sk_button(IN_X+4,y,66,TR(furniture_target_kind()),furniture_target_switchable()))furniture_toggle_target_kind();
    if(sk_button(IN_X+74,y,22,"<",true))furniture_target(-1,false);
    const char *target=furniture_target_label();int check=furniture_target_check();
    if(check>=0){sk_check(IN_X+100,y+2,check==1,p->editable);if(p->editable&&sk_hit(IN_X+98,y,18,18))furniture_target(0,true);}
    float tx=check>=0?IN_X+118:IN_X+100;
    if(sk_button(tx,y,IN_X+IN_W-30-tx,target,p->editable))furniture_target(0,true);
    if(sk_button(IN_X+IN_W-26,y,22,">",true))furniture_target(1,false);
    y+=22;
  }
  if(pages>1){
    char count[24];snprintf(count,sizeof count,"%d / %d",page+1,pages);
    if(sk_button(IN_X+4,y,42,TR("Prev"),page>0))page--;
    sk_text(count,IN_X+IN_W/2,y+3,10,SK_TEXT,1,60);
    if(sk_button(IN_X+IN_W-46,y,42,TR("Next"),page+1<pages))page++;
    y+=22;
  }
  bool can_save=p->kind==F_TROPHY?p->fields[0].editable:p->kind!=F_GIFT&&(p->editable||p->kind==F_GIFT_BUY);
  if(can_save&&sk_button(IN_X+4,IN_Y+IN_H-25,IN_W/2-6,TR(p->kind==F_GIFT_BUY?"Buy and send":p->kind==F_CLOCK?"Run command":p->kind==F_FOOTBALL_GATE?"Store kit":"Save"),true))furniture_save();
  bool apply=p->can_apply;
  if(apply&&sk_button(IN_X+IN_W/2+2,IN_Y+IN_H-25,IN_W/2-6,TR(p->kind==F_GIFT?"Open gift":"Wear outfit"),true))furniture_apply();
  if((p->kind==F_CHANGING||p->kind==F_FOOTBALL_GATE)&&sk_button(IN_X+4,IN_Y+69,IN_W-8,TR("Change clothes"),true))ui_open(P_LOOK);
  if(p->status[0])sk_text(TR(p->status),IN_X+4,IN_Y+IN_H-42,9,SK_TEXT,0,IN_W-8);
}
