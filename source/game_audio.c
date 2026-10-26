#include "game_audio.h"
#include "gen/game_audio_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __3DS__
#include <3ds.h>
static ndspWaveBuf buffers[8];
#else
#include "audio.h"
#endif
static void *samples[64];
static int playing[8];
static bool ready,muted;
static unsigned next_voice;
static void stop(int channel){
  if(!ready)return;
#ifdef __3DS__
  ndspChnWaveBufClear(channel);memset(&buffers[channel],0,sizeof buffers[channel]);
#else
  audio_stop(channel);
#endif
  playing[channel]=-1;
}
void game_audio_stop(void){for(int channel=0;channel<8;channel++)stop(channel);}
void game_audio_free(void){
  game_audio_stop();
#ifdef __3DS__
  if(ready)ndspExit();
#endif
  ready=false;
  for(unsigned i=0;i<64;i++){
#ifdef __3DS__
    linearFree(samples[i]);
#else
    free(samples[i]);
#endif
    samples[i]=NULL;
  }
}
bool game_audio_load(int bank){
  game_audio_free();if(game_sound_count>64)return false;
#ifdef __3DS__
  if(R_FAILED(ndspInit()))return false;
  for(int channel=0;channel<8;channel++){ndspChnReset(channel);ndspChnSetInterp(channel,NDSP_INTERP_POLYPHASE);ndspChnSetFormat(channel,NDSP_FORMAT_MONO_PCM16);float mix[12]={.6f,.6f};ndspChnSetMix(channel,mix);}
#else
  if(!audio_init())return false;
#endif
  ready=true;memset(playing,-1,sizeof playing);size_t total=0;
  for(unsigned i=0;i<game_sound_count;i++){
    const GameSound *sound=&game_sounds[i];if(sound->bank!=bank)continue;
    size_t bytes=(size_t)sound->samples*2;total+=bytes;if(!bytes||bytes>1024*1024||total>4*1024*1024)goto fail;
    char path[128];
#ifdef __vita__
    snprintf(path,sizeof path,"app0:%s",sound->path);
#else
    snprintf(path,sizeof path,"romfs:/%s",sound->path);
#endif
    FILE *file=fopen(path,"rb");if(!file)goto fail;
#ifdef __3DS__
    samples[i]=linearAlloc(bytes);
#else
    samples[i]=malloc(bytes);
#endif
    bool ok=samples[i]&&fread(samples[i],1,bytes,file)==bytes;fclose(file);if(!ok)goto fail;
#ifdef __3DS__
    DSP_FlushDataCache(samples[i],bytes);
#endif
  }
  return true;
fail:game_audio_free();return false;
}
void game_audio_play(const char *name,int channel,bool loop){
  if(!ready)return;
  if(channel>=0&&channel<8&&!name[0]){stop(channel);return;}
  if(muted)return;
  int index=-1;for(unsigned i=0;i<game_sound_count;i++)if(samples[i]&&!strcmp(name,game_sounds[i].name)){index=i;break;}
  if(index<0)return;
  if(channel<0)channel=4+(next_voice++%4);
  if(channel>=8)return;
  if(loop&&playing[channel]==index)return;
  stop(channel);const GameSound *sound=&game_sounds[index];
#ifdef __3DS__
  ndspChnSetRate(channel,sound->rate);buffers[channel].data_vaddr=samples[index];buffers[channel].nsamples=sound->samples;buffers[channel].looping=loop;ndspChnWaveBufAdd(channel,&buffers[channel]);
#else
  if(!audio_play(channel,samples[index],sound->samples,16,sound->rate,loop,0))return;
#endif
  playing[channel]=index;
}
void game_audio_toggle(void){muted=!muted;if(muted)game_audio_stop();}
bool game_audio_muted(void){return muted;}
