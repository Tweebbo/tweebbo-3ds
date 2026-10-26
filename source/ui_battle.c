#include "language.h"
#include <3ds.h>
#include "ui_int.h"
#include "battle_panel.h"
#include "world.h"
static u64 move_at;
void battle_ui_open(void){ui_open(P_BATTLE);battle_panel_reset();battle_open(W.me);move_at=0;}
void battle_ui_input(u32 down,u32 held,u64 now){
  if(down&KEY_B){ui_close();return;}
  if(!battle_playing())return;
  if(down&KEY_A)battle_panel_action(BATTLE_POWER,0);
  if(down&KEY_X)battle_centre();
  if(down&KEY_R)battle_zoom(1);
  if(down&KEY_L)battle_zoom(-1);
  if(now<move_at)return;
  int dx=0,dy=0;
  if(held&KEY_UP){dx--;dy--;}if(held&KEY_DOWN){dx++;dy++;}
  if(held&KEY_LEFT){dx--;dy++;}if(held&KEY_RIGHT){dx++;dy--;}
  if(dx||dy){battle_move(dx,dy);move_at=now+160;}
}
static void button(float x,float y,float width,float height,const char *text,int command,int argument){
  (void)height;if(sk_button(x,y,width,text,command!=0)){battle_panel_action(command,argument);if(!battle_active())ui_close();}
}
static void label(const char *text,float x,float y,float width){sk_text(text,x,y,10.5f,SK_TEXT,0,width);}
void battle_ui_draw(u64 now){
  (void)now;if(sk_window(WIN_X,WIN_Y,WIN_W,WIN_H,TR("Battle Ball"))){ui_close();return;}sk_content(IN_X,IN_Y,IN_W,IN_H);
  battle_panel(18,45,284,button,label);
}
