#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {unsigned fps,cpu;} PowerTarget;
typedef struct {
 uint64_t last_active,last_tick;
 bool started,applied;
 PowerTarget current;
 int32_t clock_result;
} PowerProfile;

/* CPU flags are the PTMSYSM ABI: bit 0 selects 804 MHz, bit 1 enables L2. */
static inline PowerTarget power_profile_target(PowerProfile *p,uint64_t now,bool new_model,bool saver,bool active,bool foreground_update){
 if(!p->started||now<p->last_tick||now-p->last_tick>1000||active){p->last_active=now;p->started=true;}
 p->last_tick=now;
 bool boost=new_model&&!saver&&(foreground_update||now-p->last_active<2000);
 return (PowerTarget){foreground_update?10u:boost?60u:30u,new_model?(boost?3u:2u):0u};
}

#if defined(__3DS__) || defined(POWER_PROFILE_TEST)
#include <3ds.h>
#include <citro3d.h>
static inline bool power_profile_apply(PowerProfile *p,PowerTarget target,bool new_model){
 bool changed=!p->applied||p->current.cpu!=target.cpu||p->current.fps!=target.fps;
 if(!p->applied||p->current.cpu!=target.cpu){
  p->clock_result=0;
  if(new_model){
   osSetSpeedupEnable((target.cpu&1u)!=0);
   Result result=ptmSysmInit();
   if(R_SUCCEEDED(result)){result=PTMSYSM_ConfigureNew3DSCPU((u8)target.cpu);ptmSysmExit();}
   p->clock_result=result;
  }
 }
 if(!p->applied||p->current.fps!=target.fps)C3D_FrameRate((float)target.fps);
 p->current=target;p->applied=true;
 return changed;
}
#endif
