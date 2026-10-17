#include "ui_int.h"
#include "gfx.h"
#include "net.h"
#include "world.h"
#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { SLOT, ROULETTE, BLACKJACK, CRASH, POKER, VIDEO, DASH, NO_GAME };
static const char *keys[] = {"slot", "roulette", "blackjack", "crash", "poker", "videopoker", "dash"};
static const char *titles[] = {"Slots", "Roulette", "Blackjack", "Crash", "Casino Hold'em", "Video Poker", "VIP Dash"};
static const char *risks[] = {"Bellboy", "VIP", "High Roller", "Bobba"};
static const char *slot_names[] = {"Star Treasure", "Lucky Sevens", "Gem Ways", "Candy Tumble", "Coin Rush"};
static const char *slot_paths[] = {"video", "slots-classic", "slots-ways", "slots-tumble", "slots-coins"};
static struct {
  bool active, rules;
  int kind, bet, bets[6], bet_count, slot, free_left, held, roulette_kind, roulette_value, rules_page, risk;
  u32 item;
  u64 sent_at, received_at, ready_at, animate_until;
  json_t *state, *round;
  char status[180];
} C = {.kind=NO_GAME, .bet=5};

static int number(json_t *j, const char *key) { return (int)json_integer_value(json_object_get(j, key)); }
static const char *string(json_t *j, const char *key) { const char *s=json_string_value(json_object_get(j,key)); return s?s:""; }
static void replace(json_t **dest, json_t *value) { json_incref(value); json_decref(*dest); *dest=value; }
void casino_close(bool notify) {
  if (C.active && notify) send_casino(10,0,0,0);
  json_decref(C.state); json_decref(C.round); C.state=C.round=NULL;
  C.active=false; C.kind=NO_GAME; C.sent_at=0;
}
void casino_notice(void) { C.sent_at=0; }
void casino_poll(u64 now) {
  if (C.sent_at && now-C.sent_at>5000) { C.sent_at=0; snprintf(C.status,sizeof C.status,"No result received. Reopen the machine to check your game."); }
}
static void action(int op, int a, int b, int c, u64 now) {
  send_casino(op,a,b,c); C.sent_at=now; C.ready_at=now+700;
  if (op==2) C.animate_until=now+900;
}
void casino_receive(const char *text, u64 now) {
  json_t *j=json_loads(text,0,NULL);
  if (!json_is_object(j)) { json_decref(j); return; }
  const char *type=string(j,"t");
  if (!strcmp(type,"open")) {
    int kind=NO_GAME; for(int i=0;i<NO_GAME;i++)if(!strcmp(string(j,"game"),keys[i]))kind=i;
    if(kind==NO_GAME){json_decref(j);return;}
    casino_close(false); C.kind=kind; C.item=(u32)number(j,"item"); C.active=true; C.rules=false; C.held=0;
    C.bet_count=0; json_t *v; size_t i;
    json_array_foreach(json_object_get(j,"bets"),i,v) if(C.bet_count<6 && json_integer_value(v)>0 && json_integer_value(v)<=100000) C.bets[C.bet_count++]=(int)json_integer_value(v);
    if(!C.bet_count){static const int bets[]={1,2,5,10,25,50};memcpy(C.bets,bets,sizeof bets);C.bet_count=6;}
    bool found=false;for(int b=0;b<C.bet_count;b++)found|=C.bet==C.bets[b];if(!found)C.bet=C.bets[0];
    C.free_left=number(j,"freeLeft"); if(C.free_left)C.slot=(unsigned)number(j,"freeGame")%5;
    replace(&C.state,json_object_get(j,kind==POKER?"poker":kind==VIDEO?"video":kind==DASH?"dash":"hand"));
    replace(&C.round,json_object_get(j,"round"));
    C.status[0]=0; C.ready_at=now; C.received_at=now;
    ui_open(P_CASINO);
  } else if(C.active) {
    if(!strcmp(type,"crash")) {
      if(C.kind==CRASH && (u32)number(j,"item")==C.item){replace(&C.round,json_object_get(j,"round"));C.received_at=now;C.sent_at=0;}
    } else if(!strcmp(type,"note")) { snprintf(C.status,sizeof C.status,"%s",string(j,"text"));C.sent_at=0; }
    else if((!strcmp(type,"bj")&&C.kind==BLACKJACK)||(!strcmp(type,"poker")&&C.kind==POKER)) {replace(&C.state,json_object_get(j,"hand"));C.sent_at=0;}
    else if(!strcmp(type,"vp")&&C.kind==VIDEO) { replace(&C.state,j);C.held=json_is_true(json_object_get(j,"done"))?0:number(j,"held");C.sent_at=0; }
    else if(!strcmp(type,"slot")&&C.kind==SLOT) {
      replace(&C.state,j);C.slot=(unsigned)number(j,"game")%5;C.free_left=number(j,"freeLeft");C.sent_at=0;C.received_at=now;
      snprintf(C.status,sizeof C.status,"Win: %d credits. Free spins: %d.",number(j,"win"),C.free_left);
    } else if(!strcmp(type,"dash")&&C.kind==DASH) {
      replace(&C.state,j);C.sent_at=0;C.received_at=now;
      if(json_is_true(json_object_get(j,"done"))) {
        if(json_is_true(json_object_get(j,"lost")))snprintf(C.status,sizeof C.status,"Caught on step %d! The stake of %d credits is lost.",number(j,"step")+1,number(j,"stake"));
        else snprintf(C.status,sizeof C.status,"Cashed out: %d credits.",number(j,"win"));
      } else C.status[0]=0;
    } else if(!strcmp(type,"roulette")&&C.kind==ROULETTE) {
      replace(&C.state,j);C.sent_at=0;
      snprintf(C.status,sizeof C.status,"Number %d. Win: %d credits.",number(j,"n"),number(j,"win"));
    }
  }
  json_decref(j);
}

static void card(int value,float x,float y,bool held) {
  char key[48];static const char suits[]="hdcs";
  if(value<0||value>51)snprintf(key,sizeof key,"casino-cards-back");
  else snprintf(key,sizeof key,"casino-cards-%c%d",suits[value%4],value/4+1);
  if(held)fill_round(x-3,y-3,53,72,3,sk_rgb(0xffd34d));
  sk_sprite(key,x,y,1);
}
static void cards(json_t *array,int empty,float x,float y,int spacing) {
  int n=(int)json_array_size(array);if(n<empty)n=empty;if(n>12)n=12;
  for(int i=0;i<n;i++){json_t *v=json_array_get(array,i);card(v?(int)json_integer_value(v):-1,x+i*spacing,y,false);}
}
static const char *phase(u64 now) {
  const char *p=string(C.round,"phase");
  return !strcmp(p,"betting") && now-C.received_at>=(u64)number(C.round,"startsIn")?"running":p;
}
static double multiplier(u64 now) {
  const char *p=phase(now);
  if(!strcmp(p,"crashed"))return json_number_value(json_object_get(C.round,"point"));
  if(strcmp(p,"running"))return 1;
  double ms=now-C.received_at;
  ms+=!strcmp(string(C.round,"phase"),"betting")?-number(C.round,"startsIn"):number(C.round,"elapsed");
  return exp(fmin(100000.,fmax(0.,ms))*.00006);
}
static json_t *my_bet(void) {
  json_t *v;size_t i;json_array_foreach(json_object_get(C.round,"bets"),i,v)if((u32)number(v,"eid")==W.me)return v;
  return NULL;
}
static bool red(int n) {
  static const u8 reds[]={1,3,5,7,9,12,14,16,18,19,21,23,25,27,30,32,34,36};
  for(unsigned i=0;i<sizeof reds;i++)if(n==reds[i])return true;
  return false;
}

bool casino_top(u64 now) {
  if(!C.active || ui_panel()!=P_CASINO)return false;
  fill_rect(0,0,400,240,sk_rgb(0x173c32));
  text_draw(titles[C.kind],200,5,16,sk_rgb(0xf2d27a),1);
  if(C.kind==BLACKJACK) {
    char label[80];snprintf(label,sizeof label,"Dealer: %d",number(C.state,"dealerTotal"));text_draw(label,16,28,11,sk_rgb(0xffffff),0);
    cards(json_object_get(C.state,"dealer"),2,110,30,24);
    snprintf(label,sizeof label,"You: %d",number(C.state,"total"));text_draw(label,16,132,11,sk_rgb(0xffffff),0);
    cards(json_object_get(C.state,"player"),2,110,132,24);
    text_draw("Blackjack pays 3:2. Dealer stands on 17.",200,218,11,sk_rgb(0xf2d27a),1);
  } else if(C.kind==POKER) {
    text_draw("Dealer",12,36,11,sk_rgb(0xffffff),0);cards(json_object_get(C.state,"dealer"),2,85,27,53);
    text_draw("Table",12,105,11,sk_rgb(0xffffff),0);cards(json_object_get(C.state,"board"),5,85,98,53);
    text_draw("You",12,180,11,sk_rgb(0xffffff),0);cards(json_object_get(C.state,"player"),2,85,170,53);
  } else if(C.kind==VIDEO) {
    json_t *array=json_object_get(C.state,"cards");
    for(int i=0;i<5;i++){json_t *v=json_array_get(array,i);card(v?(int)json_integer_value(v):-1,60+i*56,90,(C.held>>i)&1);}
    text_draw("Jacks or Better",200,45,14,sk_rgb(0xf2d27a),1);
    text_draw(string(C.state,"hand"),200,184,12,sk_rgb(0xffffff),1);
  } else if(C.kind==SLOT) {
    int cols=C.slot==1?3:C.slot==3?6:5,rows=C.slot==3?5:3;
    int cellw=48,cellh=C.slot==3?38:56,ox=(400-cols*cellw)/2,oy=42;
    text_draw(slot_names[C.slot],200,25,11,sk_rgb(0xffffff),1);
    json_t *grid=json_object_get(C.state,"grid");
    // Tumbles and respins show each authoritative stage, then the final board.
    json_t *steps=json_object_get(C.state,C.slot==3?"steps":"respins");
    if(json_array_size(steps)) {
      size_t at=(now-C.received_at)/450;if(at>=json_array_size(steps))at=json_array_size(steps)-1;
      json_t *stage=json_object_get(json_array_get(steps,at),"grid");if(stage)grid=stage;
    }
    for(int c=0;c<cols;c++)for(int r=0;r<rows;r++) {
      int x=ox+c*cellw,y=oy+r*cellh;
      fill_round(x,y,cellw-2,cellh-2,2,sk_rgb(0xeee7d2));
      json_t *value=json_array_get(json_array_get(grid,c),r);
      // Before the first spin, show each game's own symbols rather than a board of blanks.
      static const int symbol_count[]={12,7,9,10,7};
      int n=value?(int)json_integer_value(value):(c*3+r*2+C.slot)%symbol_count[C.slot];
      if(now<C.animate_until)n=(int)(now/75+c*3+r)%6;
      if(n==255)continue;
      char key[64],label[20];
      snprintf(key,sizeof key,"casino-%s-%d",slot_paths[C.slot],n>=100?100:n);
      int w,h;
      if(sk_sprite_size(key,&w,&h)) {
        float scale=w<=cellw-4&&h<=cellh-4?1:.5f;
        sk_sprite(key,x+(cellw-w*scale)/2,y+(cellh-h*scale)/2,scale);
      }else{
        static const char *letters[]={"10","J","Q","K","A"};
        snprintf(label,sizeof label,"%s",C.slot==0&&n<5?letters[n]:"*");text_draw(label,x+cellw/2,y+cellh/2-6,14,sk_rgb(0x442b61),1);
      }
      if(n>=100){snprintf(label,sizeof label,"%dx",n-100);text_draw(label,x+cellw/2,y+cellh-15,10,sk_rgb(0x222222),1);}
    }
  } else if(C.kind==ROULETTE) {
    for(int n=0;n<=36;n++) {
      int x=n?28+(n-1)/3*30:4,y=n?65+(n-1)%3*36:65;
      fill_round(x,y,n?28:22,n?34:106,2,sk_rgb(n==0?0x226b36:red(n)?0x9c2926:0x222222));
      char label[8];snprintf(label,sizeof label,"%d",n);text_draw(label,x+(n?14:11),y+(n?9:44),12,sk_rgb(0xffffff),1);
    }
    char result[40];snprintf(result,sizeof result,"Last number: %d",number(C.state,"n"));text_draw(C.state?result:"Choose your bet below.",200,195,13,sk_rgb(0xf2d27a),1);
  } else if(C.kind==DASH) {
    // The red carpet: one tile per step; climbed tiles glow, a caught step turns red.
    int steps=C.state?number(C.state,"steps"):(C.risk==3?10:8),step=C.state?number(C.state,"step"):0;
    bool lost=C.state&&json_is_true(json_object_get(C.state,"lost"));
    float tw=(400-40)/(float)steps;
    fill_rect(14,96,372,44,sk_rgb(0x8f1d1d));
    for(int i=0;i<steps;i++){
      u32 c=i<step?0xffd34d:(lost&&i==step)?0xff5544:0xb33a2c;
      fill_round(20+i*tw,100,tw-4,36,3,sk_rgb(c));
      char n[4];snprintf(n,sizeof n,"%d",i+1);text_draw(n,20+i*tw+(tw-4)/2,110,11,sk_rgb(0x3a1010),1);
    }
    char line[100];snprintf(line,sizeof line,"%s ladder",risks[C.risk%4]);text_draw(line,200,30,12,sk_rgb(0xffffff),1);
    if(C.state&&!json_is_true(json_object_get(C.state,"done"))){
      snprintf(line,sizeof line,"Cash out now: %d   Next step: %d",number(C.state,"cashout"),number(C.state,"next"));
      text_draw(line,200,160,12,sk_rgb(0xffd34d),1);
    }
    text_draw("Climb the carpet. One step hides a trap.",200,214,11,sk_rgb(0xf2d27a),1);
  } else if(C.kind==CRASH) {
    const char *p=phase(now);char line[100];
    snprintf(line,sizeof line,"%.2fx",multiplier(now));text_draw(line,200,40,16,sk_rgb(!strcmp(p,"crashed")?0xff9988:0xffd34d),1);
    json_t *v;size_t i;json_array_foreach(json_object_get(C.round,"bets"),i,v) {
      if(i>=10)break;
      json_t *out=json_object_get(v,"out");
      snprintf(line,sizeof line,"%s  %d credits%s",string(v,"name"),number(v,"bet"),json_is_number(out)?" - cashed out":"");
      text_draw(line,22,75+i*14,11,sk_rgb(0xffffff),0);
    }
  }
  return true;
}

static const char *rules[] = {
  "Slots use hotel credits. Choose a game before spinning. Free spins keep the game and bet that awarded them. Star Treasure has 10 lines and awards 8 free spins for 3 or more scatters. Results and payouts are calculated by the hotel server.",
  "European roulette has one zero. A number pays 36 times the bet; red, black, odd, even and either half pay 2 times. A dozen pays 3 times. Zero loses outside bets. Payouts include the original bet.",
  "Get closer to 21 than the dealer without going over. Aces count as 1 or 11. Blackjack pays 3:2; the dealer stands on 17. Double is available on the first two cards, charges another bet and draws one card. A push returns your bet.",
  "Everyone at this cabinet shares the same round. Bet before it starts. Cash out before the crash to win your bet times the multiplier. An uncashed bet is lost when it crashes. The server decides whether cash-out arrived in time.",
  "Bet the ante. You and the dealer combine two cards with five community cards. Call costs twice the ante; folding loses the ante. The dealer qualifies with a pair of fours or better. Ante pays: royal flush 100:1, straight flush 20:1, four of a kind 10:1, full house 3:1, flush 2:1, other wins 1:1.",
  "Hold any cards and draw once. Jacks or Better pays per credit: royal flush 250, straight flush 50, four of a kind 25, full house 9, flush 6, straight 4, three of a kind 3, two pair 2, jacks or better 1. Payouts include the bet.",
  "Choose a ladder and a stake, then climb the red carpet one step at a time. One step hides a trap: stepping on it loses the stake. After every safe step you can cash out for the stake times that step's multiplier; reaching the last step pays the top of the ladder. Bellboy is the safest ladder, Bobba the wildest. On Nintendo 3DS the Dash plays with credits; chips are only in the PC edition."
};

void casino_draw(u64 now) {
  if(!C.active){ui_close();return;}
  if(sk_window(WIN_X,WIN_Y,WIN_W,WIN_H,titles[C.kind])||(K.b&&!C.rules)){ui_close();return;}
  sk_content(IN_X,IN_Y,IN_W,IN_H);
  bool ready=!C.sent_at&&now>=C.ready_at;
  if(C.rules) {
    int offset=0;for(int i=0;i<C.rules_page;i++)offset=sk_paragraph_page(rules[C.kind],offset,-1000,-1000,280,11,SK_TEXT,7);
    int next=sk_paragraph_page(rules[C.kind],offset,IN_X+10,IN_Y+8,280,11,SK_TEXT,7);
    if(sk_button(20,174,85,"Previous",C.rules_page>0))C.rules_page--;
    if(sk_button(117,174,85,"Back",true)||K.b)C.rules=false;
    if(sk_button(214,174,85,"Next",rules[C.kind][next]!=0))C.rules_page++;
    return;
  }
  bool playing=C.state && ((C.kind==BLACKJACK||C.kind==POKER)?!string(C.state,"result")[0]:(C.kind==VIDEO||C.kind==DASH)?!json_is_true(json_object_get(C.state,"done")):false);
  for(int i=0;i<C.bet_count;i++) {
    char b[12];snprintf(b,sizeof b,"%d",C.bets[i]);
    if(sk_tab(IN_X+7+i*48,IN_Y+6,44,21,b,C.bet==C.bets[i])&&ready&&!playing&&!C.free_left)C.bet=C.bets[i];
  }
  // Buttons as well as touch: the D-pad changes the bet (Up/Down; roulette uses them for its number).
  if((K.up||K.dn)&&C.kind!=ROULETTE&&ready&&!playing&&!C.free_left){int b=0;while(b<C.bet_count-1&&C.bets[b]!=C.bet)b++;C.bet=C.bets[(b+(K.up?1:C.bet_count-1))%C.bet_count];}
  char line[180]="";
  if(C.kind==SLOT) {
    if((sk_button(20,78,28,"<",ready&&!C.free_left)||(K.left&&ready&&!C.free_left))) {C.slot=(C.slot+4)%5;replace(&C.state,NULL);C.status[0]=0;}
    sk_text(slot_names[C.slot],160,80,12,SK_TEXT,1,212);
    if((sk_button(272,78,28,">",ready&&!C.free_left)||(K.right&&ready&&!C.free_left))) {C.slot=(C.slot+1)%5;replace(&C.state,NULL);C.status[0]=0;}
    snprintf(line,sizeof line,"%s",C.status[0]?C.status:"Choose a game. Bet amounts are in hotel credits.");
    bool can=ready&&(C.free_left||W.credits>=(u32)C.bet);
    if(sk_button_big(20,169,190,C.free_left?"Play free spin (A)":"Spin (A)",can)||(K.a&&can))action(2,C.bet,C.slot,0,now);
  } else if(C.kind==ROULETTE) {
    static const char *choices[]={"Number","Red","Black","Odd","Even","1-18","19-36","Dozen"};
    if(sk_button(20,78,28,"<",ready)||(K.left&&ready))C.roulette_kind=(C.roulette_kind+7)%8;
    if(sk_button(272,78,28,">",ready)||(K.right&&ready))C.roulette_kind=(C.roulette_kind+1)%8;
    sk_text(choices[C.roulette_kind],160,80,12,SK_TEXT,1,200);
    int max=C.roulette_kind==7?3:37;
    C.roulette_value%=max;
    if(C.roulette_kind==0||C.roulette_kind==7){
      if(sk_button(20,103,28,"-",ready)||(K.dn&&ready))C.roulette_value=(C.roulette_value+max-1)%max;
      if(sk_button(272,103,28,"+",ready)||(K.up&&ready))C.roulette_value=(C.roulette_value+1)%max;
      snprintf(line,sizeof line,C.roulette_kind==7?"Dozen %d":"Number %d",C.roulette_value+(C.roulette_kind==7));sk_text(line,160,105,12,SK_TEXT,1,180);
    }
    snprintf(line,sizeof line,"%s",C.status);
    bool can=ready&&W.credits>=(u32)C.bet;
    if(sk_button_big(20,169,190,"Place bet (A)",can)||(K.a&&can))action(3,C.roulette_kind,C.roulette_value,C.bet,now);
  } else if(C.kind==BLACKJACK||C.kind==POKER) {
    if(playing){
      snprintf(line,sizeof line,C.kind==BLACKJACK?"Your total: %d. Hit or stand?":"Call costs %d credits, or fold.",number(C.state,C.kind==BLACKJACK?"total":"ante")*(C.kind==POKER?2:1));
      if(sk_button(20,143,85,C.kind==BLACKJACK?"Hit (A)":"Call (A)",ready)||(K.a&&ready))action(C.kind==BLACKJACK?5:12,0,0,0,now);
      else if(sk_button(113,143,85,C.kind==BLACKJACK?"Stand (X)":"Fold (X)",ready)||(K.x&&ready))action(C.kind==BLACKJACK?6:13,0,0,0,now);
      else {bool can=ready&&json_array_size(json_object_get(C.state,"player"))==2&&W.credits>=(u32)number(C.state,"bet");
        if(C.kind==BLACKJACK&&(sk_button(206,143,94,"Double (Y)",can)||(K.y&&can)))action(7,0,0,0,now);}
    }else{
      snprintf(line,sizeof line,C.state?"Result: %s. Paid: %d credits.":"Choose your bet, then deal.",string(C.state,"result"),number(C.state,"win"));
      bool can=ready&&W.credits>=(u32)C.bet;
      if(sk_button_big(20,169,190,"Deal (A)",can)||(K.a&&can))action(C.kind==BLACKJACK?4:11,C.bet,0,0,now);
    }
  } else if(C.kind==VIDEO) {
    for(int i=0;i<5;i++){char b[16];snprintf(b,sizeof b,(C.held>>i)&1?"Held %d":"Hold %d",i+1);if(sk_button(12+i*60,86,56,b,playing&&ready))C.held^=1<<i;}
    snprintf(line,sizeof line,playing?"Choose cards to hold, then draw.":C.state?"%s. Paid: %d credits.":"Jacks or Better. Choose your bet.",string(C.state,"hand"),number(C.state,"win"));
    bool can=ready&&(playing||W.credits>=(u32)C.bet);
    if(sk_button_big(20,169,190,playing?"Draw (A)":"Deal (A)",can)||(K.a&&can))action(playing?15:14,playing?C.held:C.bet,0,0,now);
  } else if(C.kind==DASH) {
    if(!playing){
      if(sk_button(20,78,28,"<",ready)||(K.left&&ready))C.risk=(C.risk+3)%4;
      if(sk_button(272,78,28,">",ready)||(K.right&&ready))C.risk=(C.risk+1)%4;
    }
    sk_text(risks[(playing?number(C.state,"risk"):C.risk)%4],160,80,12,SK_TEXT,1,200);
    if(playing)snprintf(line,sizeof line,"Step %d of %d. Cash out now for %d credits, or risk the next step for %d.",number(C.state,"step"),number(C.state,"steps"),number(C.state,"cashout"),number(C.state,"next"));
    else snprintf(line,sizeof line,"%s",C.status[0]?C.status:"Choose a ladder and a stake in credits, then start.");
    if(playing){
      if(sk_button(20,143,132,"Step (A)",ready)||(K.a&&ready))action(17,0,0,0,now);
      bool can_out=ready&&number(C.state,"step")>0;
      if(sk_button(160,143,140,"Cash out (X)",can_out)||(K.x&&can_out))action(18,0,0,0,now);
    }else{
      int index=0;while(index<C.bet_count-1&&C.bets[index]!=C.bet)index++;
      bool can=ready&&W.credits>=(u32)C.bet;
      if(sk_button_big(20,169,190,"Start (A)",can)||(K.a&&can)){replace(&C.state,NULL);action(16,C.risk,index,0,now);}
    }
  } else if(C.kind==CRASH) {
    const char *p=phase(now);json_t *mine=my_bet();bool out=json_is_number(json_object_get(mine,"out"));
    snprintf(line,sizeof line,!strcmp(p,"running")?"Cash out before it crashes!":!strcmp(p,"crashed")?"Crashed. Wait for the next round.":"Place your bet before the round starts.");
    bool can_bet=ready&&!mine&&strcmp(p,"running")&&strcmp(p,"crashed")&&W.credits>=(u32)C.bet,can_out=ready&&mine&&!out&&!strcmp(p,"running");
    if(sk_button(20,143,132,"Place bet (A)",can_bet)||(K.a&&can_bet))action(8,C.bet,0,0,now);
    if(sk_button(160,143,140,"Cash out (X)",can_out)||(K.x&&can_out))action(9,0,0,0,now);
  }
  sk_paragraph(line,IN_X+12,C.kind==ROULETTE?132:112,IN_W-24,11,SK_TEXT,2);
  if(sk_button(220,169,80,"Rules",true)){C.rules=true;C.rules_page=0;}
}
