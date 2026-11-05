#include "wired_services.h"
#include "ui_int.h"
#include "osk.h"
#include "gfx.h"
#include "language.h"
#include <stdio.h>

static int focus,field_edit;
static u32 revision;
static void edited(const char *text,void *unused){(void)unused;wired_services_edit(field_edit,text);}
void wired_services_draw3ds(u64 now){
    (void)now;wired_services_update();WiredServicePanel *panel=&wired_service_panel;
    if(revision!=panel->revision){revision=panel->revision;focus=0;}
    if(sk_window(WIN_X,WIN_Y,WIN_W,WIN_H,panel->view==WS_DETAIL||panel->view==WS_POSITION?panel->title:TR(panel->title))){wired_services_close();ui_close();return;}sk_content(IN_X,IN_Y,IN_W,IN_H);
    int pages=(panel->rows+WS_PAGE_ROWS-1)/WS_PAGE_ROWS;if(pages<1)pages=1;if(panel->page<0)panel->page=0;if(panel->page>=pages)panel->page=pages-1;
    if(panel->view==WS_LIST){
        int first=panel->page*WS_PAGE_ROWS,count=panel->rows-first;if(count>WS_PAGE_ROWS)count=WS_PAGE_ROWS;
        int controls=count+panel->button_count+2;if(K.up&&focus>0)focus--;if(K.dn&&focus+1<controls)focus++;
        for(int row=0;row<count;row++){char title[160],detail[96];wired_services_row(first+row,title,sizeof title,detail,sizeof detail);float y=IN_Y+4+row*27;
            if(focus==row)fill_rect(IN_X+2,y-1,IN_W-4,26,sk_rgb(SK_SELECT));
            sk_text(title,IN_X+7,y,11,SK_TEXT,0,IN_W-18);sk_text(detail,IN_X+7,y+13,9,SK_DIM,0,IN_W-18);
            if(sk_click(IN_X+2,y-1,IN_W-4,26)||(K.a&&focus==row)){wired_services_select(first+row);return;}
        }
        if(!count)sk_paragraph(TR(panel->body),IN_X+6,IN_Y+8,IN_W-12,10,SK_TEXT,4);
        float y=IN_Y+113;
        for(int index=0;index<panel->button_count;index++){WiredServiceButton *button=&panel->buttons[index];float width=panel->button_count==1?IN_W-92:(IN_W-96)/2;
            if(sk_button(IN_X+4+index*(width+3),y,width,button->label,button->enabled&&!panel->pending)||(K.a&&focus==count+index&&button->enabled)){wired_services_action(button->action);return;}}
        if(sk_button(IN_X+IN_W-86,y,38,TR("Prev"),panel->page>0)||(K.a&&focus==count+panel->button_count&&panel->page>0))panel->page--;
        if(sk_button(IN_X+IN_W-44,y,38,TR("Next"),panel->page+1<pages)||(K.a&&focus==count+panel->button_count+1&&panel->page+1<pages))panel->page++;
    }else{
        float y=IN_Y+4;
        if(panel->view==WS_FORM){
            sk_paragraph(TR(panel->body),IN_X+5,y,IN_W-10,10,SK_TEXT,2);y+=25;
            for(int field=0;field<panel->field_count;field++){sk_text(wired_services_field_label(field),IN_X+5,y,10,SK_DIM,0,IN_W-10);if(sk_field(IN_X+5,y+12,IN_W-10,wired_services_field_value(field),wired_services_field_label(field))&&!panel->pending){field_edit=field;osk_open(wired_services_field_label(field),wired_services_field_value(field),wired_services_field_limit(field),edited,NULL);}y+=35;}
        }else{sk_paragraph(TR(panel->body),IN_X+5,y,IN_W-10,10,SK_TEXT,panel->view==WS_CONFIRM?7:4);y+=panel->view==WS_CONFIRM?88:52;}
        if(K.up&&focus>0){focus--;}
        if(K.dn&&focus<panel->button_count){focus++;}
        for(int index=0;index<panel->button_count;index++){WiredServiceButton *button=&panel->buttons[index];float width=(IN_W-14)/2,x=IN_X+5+(index%2)*(width+4),by=y+(index/2)*23;
            if(focus==index)fill_rect(x-1,by-1,width+2,20,sk_rgb(SK_SELECT));
            if(sk_button(x,by,width,button->label,button->enabled&&!panel->pending)||(K.a&&focus==index&&button->enabled&&!panel->pending)){wired_services_action(button->action);return;}}
        if(sk_button(IN_X+5,IN_Y+IN_H-22,86,TR("Back"),!panel->pending)||(K.a&&focus==panel->button_count&&!panel->pending)){wired_services_action(WS_BACK);return;}
    }
    if(panel->status[0])sk_text(TR(panel->status),IN_X+5,IN_Y+IN_H-12,9,SK_DIM,0,IN_W-10);
}
