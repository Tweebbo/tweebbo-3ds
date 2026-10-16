#include "snow_audio.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>

static const char *names[]={"LS-miss","LS-throw","LS-hit-1","LS-hit-2","LS-hit-3","LS-walk-loop-1","LS-getsnowball","LS-C64-win-1","LS-C64-loose-1","LS-C64-draw-1"};
static struct {void *data;u32 samples;} samples[10];
static ndspWaveBuf buffers[8];
static bool ready, muted;
bool snow_audio_init(void){
  if(ready)return true;
  if(R_FAILED(ndspInit())){dbg_log("Snow Storm audio unavailable: DSP could not start");return false;}
  ready=true;ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  for(int ch=0;ch<8;ch++){
    ndspChnReset(ch);ndspChnSetInterp(ch,NDSP_INTERP_POLYPHASE);ndspChnSetRate(ch,22050);ndspChnSetFormat(ch,NDSP_FORMAT_MONO_PCM16);
    float mix[12]={.6f,.6f};ndspChnSetMix(ch,mix);
  }
  for(int i=0;i<10;i++){
    char path[96];snprintf(path,sizeof path,"romfs:/snow/%s.pcm",names[i]);FILE *f=fopen(path,"rb");if(!f)continue;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    if(n>0&&n<=256*1024&&n%2==0){samples[i].data=linearAlloc(n);if(samples[i].data){samples[i].samples=fread(samples[i].data,2,n/2,f);DSP_FlushDataCache(samples[i].data,n);}}
    fclose(f);
  }
  return true;
}
void snow_audio_stop_walk(void){if(ready){ndspChnWaveBufClear(0);memset(&buffers[0],0,sizeof buffers[0]);}}
void snow_audio_play(const char *name,bool loop){
  if(!ready||muted)return;
  int index=-1;for(int i=0;i<10;i++)if(!strcmp(name,names[i]))index=i;
  if(index<0||!samples[index].data)return;
  int ch=0;
  if(loop){if(buffers[0].status==NDSP_WBUF_PLAYING||buffers[0].status==NDSP_WBUF_QUEUED)return;}
  else{for(ch=1;ch<8;ch++)if(buffers[ch].status==NDSP_WBUF_FREE||buffers[ch].status==NDSP_WBUF_DONE)break;if(ch==8)return;}
  memset(&buffers[ch],0,sizeof buffers[ch]);buffers[ch].data_vaddr=samples[index].data;buffers[ch].nsamples=samples[index].samples;buffers[ch].looping=loop;ndspChnWaveBufAdd(ch,&buffers[ch]);
}
void snow_audio_mute(bool value){muted=value;if(ready&&muted)for(int i=0;i<8;i++)ndspChnWaveBufClear(i);}
void snow_audio_exit(void){
  if(ready){for(int i=0;i<8;i++)ndspChnWaveBufClear(i);ndspExit();ready=false;}
  for(int i=0;i<10;i++){linearFree(samples[i].data);samples[i].data=NULL;samples[i].samples=0;}
  memset(buffers,0,sizeof buffers);
}
