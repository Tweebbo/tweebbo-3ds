#define _GNU_SOURCE
#include <3ds.h>
#include "update.h"
#include "download.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
static const char *scenario;
static unsigned patchDownloads,fullDownloads,starts,writes,finishes,cancels,replaceAttempts;
static bool sleepAllowed=true,homeAllowed=true;
struct TestThread {int done;};
u64 osGetTime(void){return 1000;}
u64 now_ms(void){return osGetTime();}
void svcSleepThread(s64 ns){(void)ns;}
Result svcGetThreadPriority(s32 *priority,Handle handle){(void)handle;*priority=0x30;return 0;}
Thread threadCreate(void (*fn)(void *),void *arg,size_t stack,s32 priority,int core,bool detached){(void)stack;(void)priority;(void)core;(void)detached;Thread t=calloc(1,sizeof *t);assert(t);fn(arg);return t;}
Result threadJoin(Thread thread,u64 timeout){(void)thread;(void)timeout;return 0;}
void threadFree(Thread thread){free(thread);}
bool aptIsSleepAllowed(void){return sleepAllowed;}
void aptSetSleepAllowed(bool allowed){sleepAllowed=allowed;}
void aptSetHomeAllowed(bool allowed){homeAllowed=allowed;}
void dbg_log(const char *fmt,...){(void)fmt;}
bool store_load(const char *key,char *value,int cap){(void)key;(void)value;(void)cap;return false;}
const char *net_server(void){return "wss://tweebbo.lol";}
Result amInit(void){return 0;}
void amExit(void){}
Result AM_StartCiaInstall(int media,Handle *handle){assert(media==MEDIATYPE_SD&&!homeAllowed&&!sleepAllowed);starts++;*handle=7;return 0;}
Result FSFILE_Write(Handle handle,u32 *written,u64 offset,const void *data,u32 count,u32 flags){(void)offset;(void)data;(void)flags;assert(handle==7);writes++;*written=!strcmp(scenario,"short-write")?count-1:count;return 0;}
Result AM_FinishCiaInstall(Handle handle){assert(handle==7);finishes++;return !strcmp(scenario,"finish-fail")?-2:0;}
Result AM_CancelCIAInstall(Handle handle){assert(handle==7);cancels++;return 0;}
Result APT_PrepareToDoApplicationJump(int flags,u64 title,int media){(void)flags;(void)title;(void)media;return 0;}
void APT_DoApplicationJump(void *data,unsigned size,void *hmac){(void)data;(void)size;(void)hmac;}
int rename(const char *from,const char *to){
 if(!strcmp(from,"app.3dsx.new")&&!strcmp(to,"app.3dsx")){
  replaceAttempts++;
  if(!strcmp(scenario,"homebrew-replace-fail")||(!strcmp(scenario,"homebrew-fat")&&replaceAttempts==1)){errno=EEXIST;return -1;}
 }
 return (int)syscall(SYS_renameat,AT_FDCWD,from,AT_FDCWD,to);
}
int http_download(const char *url,HttpSink sink,void *user,long *total,char *error,int cap){
 (void)url;(void)error;(void)cap;FILE *f=fopen("manifest.json","rb");assert(f);unsigned char data[16384];size_t n=fread(data,1,sizeof data,f);fclose(f);*total=(long)n;assert(sink(data,(int)n,user));return 200;
}
int http_download_optimized_from(const char *url,long from,int connections,HttpSink sink,void *user,long *total,char *error,int cap){
 (void)error;(void)cap;assert(connections==2);bool patch=strstr(url,".tupd")!=NULL;if(patch)patchDownloads++;else fullDownloads++;
 FILE *f=fopen(patch?"payload.tupd":"payload.bin","rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);assert(from>=0&&from<=size);fseek(f,from,SEEK_SET);*total=size;
 bool drop=patch&&!strcmp(scenario,"small-interrupted")&&patchDownloads==1;long left=drop?(size-from)/2:size-from;unsigned char buffer[8192];
 while(left){size_t n=fread(buffer,1,left<(long)sizeof buffer?(size_t)left:sizeof buffer,f);assert(n);if(!strcmp(scenario,"checksum")&&!from)buffer[n-1]^=1;assert(sink(buffer,(int)n,user));left-=(long)n;from+=(long)n;}
 fclose(f);return drop?0:200;
}
static bool equal(const char *a,const char *b){FILE *x=fopen(a,"rb"),*y=fopen(b,"rb");if(!x||!y){if(x)fclose(x);if(y)fclose(y);return false;}bool ok=true;for(;;){int p=fgetc(x),q=fgetc(y);if(p!=q){ok=false;break;}if(p==EOF)break;}fclose(x);fclose(y);return ok;}
int main(int argc,char **argv){
 assert(argc==2);scenario=argv[1];bool homebrew=!strncmp(scenario,"homebrew",8);
 update_set_launch_path(homebrew?"app.3dsx":NULL);update_check();assert(update_state()==UPD_AVAILABLE);
 bool small=!strncmp(scenario,"small-",6)||!strcmp(scenario,"homebrew-small");assert(update_small()==small);update_start();
 bool fallback=!strcmp(scenario,"small-base-missing")||!strcmp(scenario,"small-base-damaged")||!strcmp(scenario,"small-invalid");
 if(fallback){assert(update_state()==UPD_FAILED&&update_full_fallback()&&!starts&&!fullDownloads);assert(strstr(update_error(),"Full download"));update_start();}
 bool failure=!strcmp(scenario,"checksum")||!strcmp(scenario,"wrong-title")||!strcmp(scenario,"short-write")||!strcmp(scenario,"finish-fail")||!strcmp(scenario,"homebrew-replace-fail");
 assert(update_state()==(failure?UPD_FAILED:UPD_INSTALLED));assert(sleepAllowed&&homeAllowed);
 if(homebrew){assert(!starts);assert(equal("app.3dsx",failure?"previous.bin":"payload.bin"));}
 else if(!failure){assert(starts==1&&finishes==1&&writes>0);}
 if(!strcmp(scenario,"wrong-title")||!strcmp(scenario,"checksum"))assert(!starts&&!writes);
 if(!strcmp(scenario,"small-interrupted"))assert(patchDownloads==2&&!fullDownloads);
 if(fallback)assert(patchDownloads==1&&fullDownloads==1);
 printf("PASS Tweebbo updater %s: patch=%u full=%u AMstarts=%u writes=%u\n",scenario,patchDownloads,fullDownloads,starts,writes);
}
