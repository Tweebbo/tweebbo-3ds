/* Fast Food uses its own JSON socket; the hotel socket remains admitted and live. */
static void food_deliver(const u8 *data,int len,void *ctx){
  (void)ctx;
  lock(&N.mu);
  if(!N.food_enabled||(ctx&&*(u32*)ctx!=N.food_gen)){unlock(&N.mu);return;}
  if(N.food_in.count>=32 || N.food_in.bytes+(size_t)len>256*1024)q_clear(&N.food_in);
  q_push(&N.food_in,data,len);unlock(&N.mu);
}
static void food_disconnected(void){
  lock(&N.mu);q_clear(&N.food_out);unlock(&N.mu);
  static const char message[]="{\"type\":\"connection\",\"connected\":false}";
  food_deliver((const u8*)message,sizeof message-1,NULL);
}
static void food_thread(void *unused){
  (void)unused;Ws ws={0};u64 retry=0;u32 generation=0;
  for(;;){
    lock(&N.mu);bool quit=N.quit,enabled=N.food_enabled,online=N.state==NET_ONLINE;
    u32 requested=N.food_gen;Endpoint ep=N.ep;char token[80];snprintf(token,sizeof token,"%s",N.token);unlock(&N.mu);
    if(quit)break;
    if(!enabled||!online||requested!=generation){
      if(ws.conn){if(!enabled)ws_send_text(&ws,(const u8*)"{\"op\":\"leave\"}",14);ws_close(&ws);if(enabled)food_disconnected();}
      generation=requested;
      if(!enabled||!online){sleep_ms(250);continue;}
      retry=0;
    }
    if(!ws.conn){
      if(now_ms()<retry){sleep_ms(100);continue;}
      char err[96],escaped[160],hello[220];
      if(!token[0]||!ws_connect(&ws,&ep,"/ws/fastfood",5000,err,sizeof err)){food_disconnected();retry=now_ms()+2000;continue;}
      ws.text_messages=true;json_escape(token,escaped,sizeof escaped);
      snprintf(hello,sizeof hello,"{\"version\":2,\"token\":\"%s\"}",escaped);
      if(!ws_send_text(&ws,(const u8*)hello,strlen(hello))){ws_close(&ws);food_disconnected();retry=now_ms()+2000;continue;}
    }
    bool alive=true;
    for(int i=0;i<8&&alive;i++){
      lock(&N.mu);Msg *m=q_pop(&N.food_out);unlock(&N.mu);if(!m)break;
      alive=ws_send_text(&ws,m->data,m->len);free(m);
    }
    if(alive)alive=ws_poll(&ws,16,food_deliver,&generation);
    if(!alive){ws_close(&ws);food_disconnected();retry=now_ms()+2000;}
  }
  ws_close(&ws);
}
bool net_fastfood_enable(bool enabled){
  if(!N.started||N.offline)return false;
  lock(&N.mu);
  if(enabled&&!N.food_thread)N.food_thread=spawn(food_thread);
  if(enabled&&!N.food_thread){unlock(&N.mu);return false;}
  if(N.food_enabled!=enabled){N.food_gen++;q_clear(&N.food_in);q_clear(&N.food_out);}
  N.food_enabled=enabled;unlock(&N.mu);return true;
}
bool net_fastfood_send(const char *json){
  if(!N.started||N.offline||!json||strlen(json)>2048)return false;
  lock(&N.mu);bool ok=N.food_enabled&&N.food_out.count<32&&q_push(&N.food_out,(const u8*)json,strlen(json));unlock(&N.mu);return ok;
}
char *net_fastfood_receive(void){
  if(!N.started||N.offline)return NULL;
  lock(&N.mu);Msg *m=q_pop(&N.food_in);unlock(&N.mu);if(!m)return NULL;
  char *s=malloc(m->len+1);if(s){memcpy(s,m->data,m->len);s[m->len]=0;}free(m);return s;
}
