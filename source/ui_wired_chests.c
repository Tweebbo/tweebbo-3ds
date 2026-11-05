#include "wired_chests.h"
#include "ui_int.h"
#include "osk.h"
#include "gfx.h"
#include "language.h"

static int focus,editing;
static u32 revision;
static void edited(const char *text,void *unused){(void)unused;wired_chests_edit(editing,text);}
static void action(int command){wired_chests_action(command);if(!wired_chest_panel.open)ui_close();}
void wired_chests_draw3ds(u64 now){
    (void)now;wired_chests_update();WiredChestPanel *p=&wired_chest_panel;if(revision!=p->revision){revision=p->revision;focus=0;}
    if(sk_window(WIN_X,WIN_Y,WIN_W,WIN_H,p->view==WC_NETWORK_DETAIL?p->title:TR(p->title))){wired_chests_close();ui_close();return;}sk_content(IN_X,IN_Y,IN_W,IN_H);
    if(K.b){if(p->view==WC_MAIN||p->view==WC_RESULT){wired_chests_close();ui_close();}else action(WC_BACK);return;}
    bool list=p->view!=WC_MAIN&&p->view!=WC_FORM&&p->view!=WC_REVIEW&&p->view!=WC_RESULT;
    if(list){int pages=(p->rows+WC_PAGE_ROWS-1)/WC_PAGE_ROWS;if(pages<1)pages=1;if(p->page<0)p->page=0;if(p->page>=pages)p->page=pages-1;
        int first=p->page*WC_PAGE_ROWS,count=p->rows-first;if(count>WC_PAGE_ROWS)count=WC_PAGE_ROWS;int controls=count+p->button_count+2;
        if(K.up&&focus>0)focus--;if(K.dn&&focus+1<controls)focus++;
        for(int row=0;row<count;row++){char title[192],detail[128];wired_chests_row(first+row,title,sizeof title,detail,sizeof detail);float y=IN_Y+3+row*26;
            if(focus==row)fill_rect(IN_X+2,y-1,IN_W-4,25,sk_rgb(SK_SELECT));int check=wired_chests_row_check(first+row);float tx=IN_X+6;if(check>=0){sk_check(IN_X+6,y+(detail[0]?5:-1),check==1,!p->pending);tx+=19;}
            sk_text(title,tx,y,10,SK_TEXT,0,IN_X+IN_W-6-tx);if(detail[0])sk_text(detail,tx,y+12,9,SK_DIM,0,IN_X+IN_W-6-tx);
            if(!p->pending&&wired_chests_row_enabled(first+row)&&(sk_click(IN_X+2,y-1,IN_W-4,25)||(K.a&&focus==row))){wired_chests_select(first+row);return;}}
        if(!count)sk_paragraph(TR(p->pending?"Loading furniture...":"No furniture here. Use Back to choose another action."),IN_X+6,IN_Y+8,IN_W-12,10,SK_TEXT,4);
        float y=IN_Y+110,available=IN_W-92;for(int i=0;i<p->button_count;i++){WiredChestButton *b=&p->buttons[i];float width=available/p->button_count-3;
            if(sk_button(IN_X+4+i*(width+3),y,width,b->label,b->enabled&&!p->pending)||(K.a&&focus==count+i&&b->enabled&&!p->pending)){action(b->action);return;}}
        if(sk_button(IN_X+IN_W-84,y,38,TR("Prev"),!p->pending&&wired_chests_has_page(-1))||(K.a&&focus==count+p->button_count&&!p->pending&&wired_chests_has_page(-1))){wired_chests_page(-1);return;}
        if(sk_button(IN_X+IN_W-43,y,38,TR("Next"),!p->pending&&wired_chests_has_page(1))||(K.a&&focus==count+p->button_count+1&&!p->pending&&wired_chests_has_page(1))){wired_chests_page(1);return;}
    }else{float y=IN_Y+4;int fields=p->view==WC_FORM?p->field_count:0,controls=fields+p->button_count;if(K.up&&focus>0)focus--;if(K.dn&&focus+1<controls)focus++;
        sk_paragraph(p->kind==WC_LABEL&&p->view==WC_MAIN?p->body:TR(p->body),IN_X+5,y,IN_W-10,10,SK_TEXT,p->view==WC_FORM?2:4);y+=p->view==WC_FORM?27:52;
        for(int field=0;field<fields;field++){sk_text(wired_chests_field_label(field),IN_X+5,y,10,SK_DIM,0,IN_W-10);bool pressed=sk_field(IN_X+5,y+12,IN_W-10,wired_chests_field_value(field),wired_chests_field_label(field));if((pressed||(K.a&&focus==field))&&!p->pending){editing=field;osk_open(wired_chests_field_label(field),wired_chests_field_value(field),wired_chests_field_limit(field),edited,NULL);}y+=35;}
        for(int i=0;i<p->button_count;i++){WiredChestButton *b=&p->buttons[i];float width=(IN_W-14)/2,x=IN_X+5+(i%2)*(width+4),by=y+(i/2)*22;if(focus==fields+i)fill_rect(x-1,by-1,width+2,20,sk_rgb(SK_SELECT));if(sk_button(x,by,width,b->label,b->enabled&&!p->pending)||(K.a&&focus==fields+i&&b->enabled&&!p->pending)){action(b->action);return;}}
    }
    if(p->status[0])sk_text(TR(p->status),IN_X+5,IN_Y+IN_H-12,9,SK_DIM,0,IN_W-10);
}
