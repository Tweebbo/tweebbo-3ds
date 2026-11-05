#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/net.c"

void sim_receive(const u8 *data,int len){(void)data;(void)len;assert(!"Furniture requests require an online hotel");}

int main(void){
  mutex_init(&N.mu);N.state=NET_ONLINE;N.offline=false;
  char *text=malloc(16386);assert(text);
  for(int size=1000;size<=16384;size=size==1000?4000:16384){
    memset(text,'x',(size_t)size);text[size]=0;send_furniture(text);
    Msg *message=q_pop(&N.out);assert(message&&message->len>size);
    Reader reader;r_init(&reader,message->data,message->len);
    assert(r_u8(&reader)==C_FURNITURE);assert(r_var(&reader)==(u64)size);
    assert(reader.p+size==message->len&&!memcmp(message->data+reader.p,text,(size_t)size));
    memset(text,'z',(size_t)size);assert(message->data[reader.p]=='x');free(message);
    if(size==16384)break;
  }
  memset(text,'x',16385);text[16385]=0;send_furniture(text);assert(!q_pop(&N.out));
  free(text);puts("Native furniture transport: 1,000, 4,000 and 16,384 bytes preserved, over-limit requests rejected, queued payload ownership retained.");
  return 0;
}
