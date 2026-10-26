import {Canvas,Sprite} from './canvas';
import {Engine,STEP_MS,type Action} from 'fastfood-original/engine';
import {View} from 'fastfood-original/view';
import {eventSound} from 'fastfood-original/audio';
import lookup from 'fastfood-lookup';
declare const nativeSendText:(data:string)=>void;
declare const nativeDisconnect:()=>void;
declare const nativeSound:(name:string,channel:number,loop:boolean)=>void;
const assets=new Map(Object.entries(lookup.sprites).map(([name,s]:[string,any])=>[name,{image:new Sprite(s.ids,s.width,s.height,lookup.resolution),offset:s.offset}]));
const canvas=new Canvas();canvas.live=true;
const view=new View(canvas as any,assets as any);view.resize(400,240);
let engine:Engine|null=null,last=0,clock=0,acc=0,slot=0,online=false,connected=false,terminal=false,lobby:any=null,error='';
let phase='',seen=new Set<string>();
function sound(){
  const next=engine?.phase??'menu';
  if(next!==phase){phase=next;nativeSound('',3,false);const file=next==='menu'?'tunnari_long.mp3':next==='countdown'?'countdown_b.mp3':next==='results'?'tunnari.mp3':'';if(file)nativeSound(file,3,false);}
  for(let i=0;i<3;i++)nativeSound(engine?.players[i]?.plate?.shield&&engine.phase==='playing'?'shield_loop.mp3':'',i,true);
  if(!engine)return;
  for(const event of engine.events){const key=`${event.tick}/${event.player}/${event.kind}/${event.detail??''}`;if(seen.has(key))continue;seen.add(key);if(engine.tick-event.tick<=12){const file=eventSound(event);if(file)nativeSound(file,-1,false);}}
  if(seen.size>1000)seen=new Set(engine.events.map(e=>`${e.tick}/${e.player}/${e.kind}/${e.detail??''}`));
}
const FastFood={
  open(){engine=null;online=connected=terminal=false;lobby=null;error='';phase='';seen.clear();},
  connect(){this.open();online=true;},
  practice(name:string,look:number,goal:number,now:number){this.open();engine=new Engine(123456789,name,true,goal);engine.players[0].look=look;view.localPlayer=slot=0;last=now;acc=0;},
  receive(raw:string,now:number){
    const m=JSON.parse(raw);
    if(m.type==='connection'){
      connected=m.connected===true;
      if(!connected){engine=null;lobby=null;if(!terminal)error='Connection lost. Reconnecting...';}
    }else if(m.type==='lobby'){
      if(!Array.isArray(m.games)||m.games.length>64)throw Error('Invalid Fast Food lobby');
      lobby=m;online=connected=true;error='';if(!m.current)engine=null;
    }else if(m.type==='state'){
      if(!m.state||![6,12,18].includes(m.state.goal)||!Array.isArray(m.state.players)||m.state.players.length<2||m.state.players.length>3||!Number.isInteger(m.slot)||m.slot<0||m.slot>=m.state.players.length||!['countdown','playing','results'].includes(m.state.phase))throw Error('Invalid Fast Food state');
      const prior=engine;engine=m.state;
      for(const p of engine!.players){const old=prior?.players[p.id]?.plate;if(p.plate)p.plate.previousY=old?.id===p.plate.id?old.y:p.plate.y;p.wrecks=(p.wrecks||[]).map((w:any)=>({type:w.type??w.dish,y:w.y}));}
      view.localPlayer=slot=m.slot;online=connected=true;last=now;
    }else if(m.type==='error'){
      error=String(m.message||'Fast Food request failed.');
      if(m.terminal){terminal=true;connected=false;engine=null;lobby=null;nativeDisconnect();}
    }
  },
  tick(now:number){clock=now;if(engine&&!online){acc+=Math.min(100,Math.max(0,now-last));last=now;while(acc>=STEP_MS){engine.step();acc-=STEP_MS;}}sound();},
  action(action:Action){if(online){if(connected&&!terminal)nativeSendText(JSON.stringify({op:'action',action}));}else engine?.action(slot,action);},
  command(json:string){if(connected&&!terminal){error='';nativeSendText(json);}},
  resize(width:number,height:number){view.resize(width,height);},
  draw(){view.render(engine,online?Math.min(1,Math.max(0,(clock-last)/96)):acc/STEP_MS);},
  status(){return JSON.stringify({online,connected,terminal,slot,lobby,error,state:engine});},
  leave(){if(online&&connected&&!terminal)nativeSendText('{"op":"leave"}');engine=null;if(!online)lobby=null;error='';},
};
(globalThis as any).FastFood=FastFood;
(globalThis as any).FastFoodTest={Engine};
