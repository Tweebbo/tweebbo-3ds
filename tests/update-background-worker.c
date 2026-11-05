#include "update.h"
#include "download.h"
#include <3ds.h>
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *scenario;
static bool sleep_allowed=true,home_allowed=true;
static unsigned starts,finishes,cancels,writes;
struct TestThread {pthread_t id;void (*entry)(void *);void *arg;};
static void *run(void *p){Thread t=p;t->entry(t->arg);return NULL;}
Thread threadCreate(void (*entry)(void *),void *arg,size_t stack,s32 priority,int core,bool detached){
 (void)stack;(void)priority;(void)core;assert(!detached);Thread t=calloc(1,sizeof *t);assert(t);t->entry=entry;t->arg=arg;assert(!pthread_create(&t->id,NULL,run,t));return t;
}
Result threadJoin(Thread t,u64 timeout){(void)timeout;return pthread_join(t->id,NULL);}
void threadFree(Thread t){free(t);}
u64 now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (u64)t.tv_sec*1000+t.tv_nsec/1000000;}
u64 osGetTime(void){return now_ms();}
void svcSleepThread(s64 ns){struct timespec t={ns/1000000000,ns%1000000000};nanosleep(&t,NULL);}
Result svcGetThreadPriority(s32 *p,Handle h){(void)h;*p=0x30;return 0;}
bool aptIsSleepAllowed(void){return sleep_allowed;}
void aptSetSleepAllowed(bool x){sleep_allowed=x;}
void aptSetHomeAllowed(bool x){home_allowed=x;}
const char *net_server(void){return "ws://127.0.0.1:18103";}
bool store_load(const char *key,char *out,int cap){(void)key;(void)out;(void)cap;return false;}
void dbg_log(const char *format,...){(void)format;}
Result amInit(void){assert(update_blocks_exit()&&!sleep_allowed&&!home_allowed);return 0;}
void amExit(void){}
Result AM_StartCiaInstall(int media,Handle *out){assert(media==MEDIATYPE_SD);starts++;*out=42;return 0;}
Result FSFILE_Write(Handle h,u32 *written,u64 at,const void *data,u32 bytes,u32 flags){
 (void)data;assert(h==42&&!flags&&update_blocks_exit()&&at==(u64)update_done_bytes());writes++;*written=!strcmp(scenario,"short-write")?bytes-1:bytes;return !strcmp(scenario,"write-failure")?-10:0;
}
Result AM_FinishCiaInstall(Handle h){assert(h==42);finishes++;return !strcmp(scenario,"finish-failure")?-11:0;}
Result AM_CancelCIAInstall(Handle h){assert(h==42);cancels++;return 0;}
Result APT_PrepareToDoApplicationJump(int flags,u64 title,int media){(void)flags;(void)title;(void)media;return 0;}
void APT_DoApplicationJump(void *data,unsigned size,void *hmac){(void)data;(void)size;(void)hmac;}
bool conn_cancelled(const ConnCancel *c){return c&&c->requested&&c->requested(c->user);}
int http_download(const char *url,HttpSink sink,void *ctx,long *total,char *error,int cap){
 (void)url;(void)error;(void)cap;FILE *f=fopen("manifest.json","rb");assert(f);u8 b[8192];size_t n=fread(b,1,sizeof b,f);fclose(f);assert(sink(b,(int)n,ctx));*total=(long)n;return 200;
}
int http_download_from_cancel(const char *url,long from,HttpSink sink,void *ctx,long *total,char *error,int cap,const ConnCancel *cancel){assert(from==0);if(conn_cancelled(cancel))return 0;return http_download(url,sink,ctx,total,error,cap);}
int http_download_optimized_from_cancel(const char *url,long from,int connections,HttpSink sink,void *ctx,long *total,char *error,int cap,const ConnCancel *cancel){
 (void)connections;(void)error;(void)cap;if(strstr(scenario,"full-choice"))assert(!strstr(url,".tupd"));if(strstr(scenario,"resume"))assert(from>0);FILE *f=fopen("payload.bin","rb");assert(f);fseek(f,0,SEEK_END);*total=ftell(f);assert(from>=0&&from<=*total);fseek(f,from,SEEK_SET);u8 b[16384];size_t n;
 while((n=fread(b,1,sizeof b,f))){if(conn_cancelled(cancel)||!sink(b,(int)n,ctx)){fclose(f);return 0;}svcSleepThread(1000000);}
 fclose(f);return from?206:200;
}
static void wait_for(UpdateState state){u64 end=now_ms()+10000;while(update_state()!=state){assert(now_ms()<end);svcSleepThread(1000000);}svcSleepThread(5000000);}
static bool original(void){char b[32]={0};FILE *f=fopen("app.3dsx","rb");assert(f);size_t n=fread(b,1,sizeof b,f);fclose(f);return n==17&&!memcmp(b,"3DSX previous app",17);}
int main(int argc,char **argv){
 assert(argc==2);scenario=argv[1];bool cia=strncmp(scenario,"homebrew",8)!=0&&strcmp(scenario,"pause")&&strcmp(scenario,"cancel");
 update_set_launch_path(cia?NULL:"app.3dsx");update_activity(0,false,false,false);update_check();wait_for(UPD_AVAILABLE);
 if(strstr(scenario,"full-choice")){assert(update_has_small()&&update_small());update_use_full(true);assert(!update_small()&&update_has_small());}
 update_start();
 assert(sleep_allowed&&home_allowed&&!update_blocks_exit());
 if(!strcmp(scenario,"pause")){
  u64 end=now_ms()+2000;while(!update_done_bytes()){assert(now_ms()<end);svcSleepThread(1000000);}
  update_pause(true);svcSleepThread(30000000);long before=update_done_bytes();svcSleepThread(80000000);assert(update_done_bytes()==before&&update_paused());update_pause(false);
 }
 if(!strcmp(scenario,"cancel")){svcSleepThread(30000000);update_shutdown();assert(original()&&!starts&&!finishes);puts("PASS background cancellation preserves app and partial file");return 0;}
 wait_for(UPD_READY);assert(original()&&!starts&&!finishes);update_check();assert(update_state()==UPD_READY);
 if(strstr(scenario,"corrupt")){const char *path=cia?DATA_DIR "/update.cia":"app.3dsx.new";FILE *f=fopen(path,"r+b");assert(f);fseek(f,100000,SEEK_SET);assert(fputc(0xff,f)!=EOF);fclose(f);}
 update_install();bool failure=strstr(scenario,"corrupt")||strstr(scenario,"failure")||!strcmp(scenario,"short-write");wait_for(failure?UPD_FAILED:UPD_INSTALLED);
 assert(sleep_allowed&&home_allowed);
 if(cia){assert(original());if(strstr(scenario,"corrupt"))assert(!starts&&!writes&&!finishes&&!cancels);else{assert(starts==1&&writes>0);if(failure)assert(!finishes||!strcmp(scenario,"finish-failure"));else assert(finishes==1&&!cancels);if(!strcmp(scenario,"short-write")||!strcmp(scenario,"write-failure"))assert(cancels==1);}}
 else assert(original()==failure);
 update_shutdown();printf("PASS prepare/ready/explicit commit %s\n",scenario);return 0;
}
