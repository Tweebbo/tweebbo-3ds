#include "wired_boards.h"
#include "net.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WiredServicePanel wired_board_panel;
static json_t *boards,*scores;
static int selected=-1,variable_index,chunk;
static json_int_t total;
static u64 sent_at;
static const char *string(json_t *row,const char *key){const char *s=json_string_value(json_object_get(row,key));return s?s:"";}
static json_int_t integer(json_t *row,const char *key){return json_integer_value(json_object_get(row,key));}
static void copy(char *out,size_t cap,const char *s){if(!cap)return;size_t n=strlen(s);if(n>=cap){n=cap-1;while(n&&((unsigned char)s[n]&0xc0)==0x80)n--;}memcpy(out,s,n);out[n]=0;}
static json_t *board(void){return selected<0?NULL:json_array_get(boards,(size_t)selected);}
static json_t *choices(void){return json_object_get(board(),"choices");}
static json_t *choice(void){return json_array_get(choices(),(size_t)variable_index);}
static void button(int action,const char *label,bool enabled){WiredServicePanel *p=&wired_board_panel;if(p->button_count>=WS_BUTTONS)return;WiredServiceButton *b=&p->buttons[p->button_count++];b->action=action;b->enabled=enabled;copy(b->label,sizeof b->label,label);}
static void describe(void){
    WiredServicePanel *p=&wired_board_panel;p->view=WS_LIST;p->button_count=0;p->field_count=0;
    if(!board()){selected=-1;copy(p->title,sizeof p->title,"Room leaderboards");copy(p->body,sizeof p->body,"No leaderboards are configured in this room.");p->rows=(int)json_array_size(boards);button(WB_REFRESH,"Refresh",!p->pending);}
    else{
        copy(p->title,sizeof p->title,string(board(),"title"));p->rows=(int)json_array_size(scores);snprintf(p->body,sizeof p->body,"%s\nScore column: %s%s",string(board(),"description"),string(choice(),"name"),!p->rows&&!p->pending?"\nNo scores on this page.":"");
        button(WB_BACK,"Boards",!p->pending);button(WB_PREVIOUS_VARIABLE,"Previous column",!p->pending&&json_array_size(choices())>1);button(WB_NEXT_VARIABLE,"Next column",!p->pending&&json_array_size(choices())>1);button(WB_PREVIOUS_CHUNK,"Earlier scores",!p->pending&&chunk>0);button(WB_NEXT_CHUNK,"Later scores",!p->pending&&total>(json_int_t)(chunk+1)*50);
    }
    p->revision++;
}
static void request(void){
    WiredServicePanel *p=&wired_board_panel;json_t *request;
    if(!board())request=json_pack("{s:s,s:i}","op","wired_leaderboard","overlay",0);
    else{request=json_pack("{s:s,s:I,s:I,s:I,s:[i]}","op","wired_leaderboard","overlay",integer(board(),"overlay"),"revision",integer(board(),"revision"),"variable",integer(choice(),"id"),"chunks",chunk);}
    char *text=json_dumps(request,JSON_COMPACT);json_decref(request);if(!text)return;
    if(!net_online()){free(text);copy(p->status,sizeof p->status,"Connect to the hotel to load scores.");return;}
    send_furniture(text);free(text);p->pending=true;sent_at=now_ms();copy(p->status,sizeof p->status,"Loading scores...");describe();
}
void wired_boards_open(void){wired_board_panel.open=true;selected=-1;variable_index=chunk=0;wired_board_panel.page=0;describe();request();}
void wired_boards_close(void){wired_board_panel.open=false;wired_board_panel.pending=false;}
void wired_boards_reset(void){json_decref(boards);json_decref(scores);boards=scores=NULL;selected=-1;variable_index=chunk=0;memset(&wired_board_panel,0,sizeof wired_board_panel);}
void wired_boards_update(void){WiredServicePanel *p=&wired_board_panel;if(p->pending&&now_ms()-sent_at>=8000){p->pending=false;copy(p->status,sizeof p->status,"Scores did not arrive. Choose Refresh or reopen the board.");describe();}}
bool wired_boards_receive(const char *text){
    if(!strstr(text,"wired_leaderboard"))return false;
    json_error_t error;json_t *data=json_loads(text,JSON_REJECT_DUPLICATES,&error);if(!data)return false;
    const char *type=string(data,"type");bool handled=false;
    if(!strcmp(type,"wired_leaderboards")){
        json_t *list=json_object_get(data,"boards");if(json_is_array(list)&&json_array_size(list)<=100){json_incref(list);json_decref(boards);boards=list;json_decref(scores);scores=NULL;selected=-1;wired_board_panel.pending=false;wired_board_panel.page=0;wired_board_panel.status[0]=0;describe();handled=true;}
    }else if(!strcmp(type,"wired_leaderboard_rows")){
        handled=true;
        if(board()&&integer(data,"overlay")==integer(board(),"overlay")&&integer(data,"revision")==integer(board(),"revision")&&integer(data,"variable")==integer(choice(),"id")&&integer(data,"chunk")==chunk){json_t *list=json_object_get(data,"rows");if(json_is_array(list)&&json_array_size(list)<=50){json_incref(list);json_decref(scores);scores=list;total=integer(data,"total");wired_board_panel.pending=false;wired_board_panel.page=0;wired_board_panel.status[0]=0;describe();}}
    }
    json_decref(data);return handled;
}
void wired_boards_select(int index){if(board()||wired_board_panel.pending||index<0||(size_t)index>=json_array_size(boards))return;selected=index;variable_index=chunk=0;json_t *options=choices();for(size_t i=0;i<json_array_size(options);i++)if(integer(json_array_get(options,i),"id")==integer(board(),"default"))variable_index=(int)i;json_decref(scores);scores=NULL;wired_board_panel.page=0;request();}
void wired_boards_action(int action){
    if(wired_board_panel.pending)return;
    if(action==WB_BACK){selected=-1;wired_board_panel.page=0;describe();return;}
    if(action==WB_PREVIOUS_VARIABLE||action==WB_NEXT_VARIABLE){size_t count=json_array_size(choices());if(!count)return;variable_index=(variable_index+(action==WB_NEXT_VARIABLE?1:(int)count-1))%(int)count;chunk=0;}
    else if(action==WB_PREVIOUS_CHUNK){if(!chunk)return;chunk--;}
    else if(action==WB_NEXT_CHUNK){if(total<=(json_int_t)(chunk+1)*50)return;chunk++;}
    else if(action!=WB_REFRESH)return;
    json_decref(scores);scores=NULL;wired_board_panel.page=0;request();
}
void wired_boards_row(int index,char *title,size_t title_size,char *detail,size_t detail_size){
    json_t *row=json_array_get(board()?scores:boards,(size_t)(index<0?0:index));if(!row){copy(title,title_size,"");copy(detail,detail_size,"");return;}
    if(!board()){copy(title,title_size,string(row,"title"));copy(detail,detail_size,string(row,"description"));}
    else{snprintf(title,title_size,"%lld. %s",(long long)integer(row,"rank"),string(row,"name"));json_t *value=json_object_get(row,"value");if(json_is_string(value))snprintf(detail,detail_size,"%s: %s",string(choice(),"name"),json_string_value(value));else snprintf(detail,detail_size,"%s: %lld",string(choice(),"name"),(long long)json_integer_value(value));}
}
