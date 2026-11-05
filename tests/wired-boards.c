#include "wired_boards.h"
#include <jansson.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static u64 clock_ms;
static char sent[2048];
u64 now_ms(void){return clock_ms;}
bool net_online(void){return true;}
void send_furniture(const char *text){snprintf(sent,sizeof sent,"%s",text);}
static void expect_request(int variable,int chunk){json_error_t error;json_t *request=json_loads(sent,0,&error);assert(request);assert(json_integer_value(json_object_get(request,"overlay"))==77);assert(json_integer_value(json_object_get(request,"revision"))==4);assert(json_integer_value(json_object_get(request,"variable"))==variable);assert(json_integer_value(json_array_get(json_object_get(request,"chunks"),0))==chunk);json_decref(request);}
int main(void){
    wired_boards_reset();wired_boards_open();assert(wired_board_panel.open&&wired_board_panel.pending);
    assert(wired_boards_receive("{\"type\":\"wired_leaderboards\",\"boards\":[{\"overlay\":77,\"revision\":4,\"title\":\"Weekly games\",\"description\":\"Room scores\",\"default\":700,\"choices\":[{\"id\":700,\"name\":\"Wins\"},{\"id\":701,\"name\":\"Points\"}]}]}"));
    assert(wired_board_panel.rows==1&&!wired_board_panel.pending);wired_boards_select(0);expect_request(700,0);
    assert(wired_boards_receive("{\"type\":\"wired_leaderboard_rows\",\"overlay\":77,\"revision\":3,\"variable\":700,\"chunk\":0,\"total\":1,\"rows\":[{\"rank\":1,\"name\":\"Stale\",\"value\":\"2\"}]}"));assert(wired_board_panel.pending);
    assert(wired_boards_receive("{\"type\":\"wired_leaderboard_rows\",\"overlay\":77,\"revision\":4,\"variable\":700,\"chunk\":0,\"total\":55,\"rows\":[{\"rank\":1,\"name\":\"Room player\",\"value\":\"9223372036854775807\"}]}"));
    char title[160],detail[96];wired_boards_row(0,title,sizeof title,detail,sizeof detail);assert(strstr(title,"Room player"));assert(strstr(detail,"9223372036854775807"));assert(!wired_board_panel.pending);
    wired_boards_action(WB_NEXT_CHUNK);expect_request(700,1);clock_ms=8001;wired_boards_update();assert(!wired_board_panel.pending&&strstr(wired_board_panel.status,"did not arrive"));
    wired_boards_action(WB_NEXT_VARIABLE);expect_request(701,0);
    assert(wired_boards_receive("{\"type\":\"wired_leaderboard_rows\",\"overlay\":77,\"revision\":4,\"variable\":701,\"chunk\":0,\"total\":0,\"rows\":[]}"));assert(!wired_board_panel.pending&&wired_board_panel.rows==0);
    wired_boards_action(WB_BACK);assert(wired_board_panel.rows==1);wired_boards_close();assert(!wired_board_panel.open);wired_boards_reset();
    puts("PASS: native leaderboard selection, revision rejection, 50-row chunk navigation, exact 64-bit scores, timeout recovery and cleanup.");
}
