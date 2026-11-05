#include "wired_boards.h"
#include "ui_int.h"
#include "gfx.h"
#include <stdio.h>

static int focus;
static u32 revision;
void wired_boards_draw3ds(u64 now){
    (void)now;wired_boards_update();WiredServicePanel *panel=&wired_board_panel;
    if(revision!=panel->revision){revision=panel->revision;focus=0;}
    if(sk_window(WIN_X,WIN_Y,WIN_W,WIN_H,panel->title)){wired_boards_close();ui_close();return;}sk_content(IN_X,IN_Y,IN_W,IN_H);
    int pages=(panel->rows+1)/2;if(pages<1)pages=1;if(panel->page<0)panel->page=0;if(panel->page>=pages)panel->page=pages-1;
    int first=panel->page*2,count=panel->rows-first;if(count>2)count=2;bool choosing=panel->button_count==1;
    int controls=(choosing?count:0)+panel->button_count+2;if(K.up&&focus>0)focus--;if(K.dn&&focus+1<controls)focus++;
    for(int row=0;row<count;row++){char title[160],detail[160];wired_boards_row(first+row,title,sizeof title,detail,sizeof detail);float y=IN_Y+3+row*25;
        if(choosing&&focus==row){fill_rect(IN_X+2,y-1,IN_W-4,25,sk_rgb(SK_SELECT));}
        sk_text(title,IN_X+6,y,11,SK_TEXT,0,IN_W-14);sk_text(detail,IN_X+6,y+13,9,SK_DIM,0,IN_W-14);
        if(choosing&&!panel->pending&&(sk_click(IN_X+2,y-1,IN_W-4,25)||(K.a&&focus==row))){wired_boards_select(first+row);return;}
    }
    if(!count)sk_paragraph(panel->status[0]?panel->status:panel->body,IN_X+6,IN_Y+5,IN_W-12,10,SK_TEXT,4);
    int offset=choosing?count:0;
    for(int index=0;index<panel->button_count;index++){WiredServiceButton *button=&panel->buttons[index];float width=(IN_W-14)/2,x=IN_X+5+(index%2)*(width+4),y=IN_Y+56+(index/2)*22;
        if(focus==offset+index){fill_rect(x-1,y-1,width+2,20,sk_rgb(SK_SELECT));}
        if(sk_button(x,y,width,button->label,button->enabled&&!panel->pending)||(K.a&&focus==offset+index&&button->enabled&&!panel->pending)){wired_boards_action(button->action);return;}}
    if(sk_button(IN_X+5,IN_Y+IN_H-23,68,"Prev rows",panel->page>0)||(K.a&&focus==offset+panel->button_count&&panel->page>0))panel->page--;
    if(sk_button(IN_X+IN_W-78,IN_Y+IN_H-23,72,"Next rows",panel->page+1<pages)||(K.a&&focus==offset+panel->button_count+1&&panel->page+1<pages))panel->page++;
}
