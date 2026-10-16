import { Canvas, NativeSprite, rgb } from './canvas';
import { Engine, TILE, SUBTURNS_PER_TURN, SUBTURN_MS, leadPoint } from 'snow-original/engine';
const TURN_MS=SUBTURNS_PER_TURN*SUBTURN_MS;
import { Assets } from 'snow-original/assets';
import { View } from 'snow-original/view';
import data from 'snow-data';
import sheet from 'snow-sheet';
import lookup from 'snow-lookup';
declare const nativeSend: (data: ArrayBuffer) => void;
declare const nativeSound: (name: string, loop: number) => void;
declare const nativeStopWalk: () => void;

const assets=new Assets(data,sheet.sprites,[]);
class FigureCache extends Map {
  set(key:any,value:any){if(this.size>=128&&!this.has(key))this.delete(this.keys().next().value);return super.set(key,value);}
}
(assets as any).figures=new FigureCache();
const members=new Map<string,NativeSprite>();
assets.member=function(name: string, ink=0, color?: string) {
  const id=lookup[`${name}/${ink}`];
  if(id===undefined)return null;
  const key=`${id}/${color||''}`;
  if(!members.has(key)) {
    const s=sheet.sprites[name];
    members.set(key,new NativeSprite(id,s.w,s.h,s.bpp<=8&&color?rgb(color):0xffffff));
  }
  return members.get(key);
} as any;
const view=new View(assets);
(view.canvas as unknown as Canvas).live=true;
view.resize(400,240);view.cam.zoom=view.cam.user=1;
let engine: Engine|null=null, me=0, game=0, nextTurn=0, rosterIds:number[]=[], pending:any[][]=[];
let games:any[]=[], last=0, acc=0, received=0, syncAt=0, trajectory=0, aim=0;
let chats:string[]=[], settings={arena:1,teams:2,players:3,minutes:3,level:1,fill:2};
let local=false, localTurn=0, ended=0;
class Reader {
  p=0; b:Uint8Array;
  constructor(b:ArrayBuffer){this.b=new Uint8Array(b);}
  u(){if(this.p>=this.b.length)throw Error('Truncated Snow Storm packet');return this.b[this.p++];}
  v(){let v=0,m=1;for(let i=0;i<7;i++){const b=this.u();v+=(b&127)*m;if(!(b&128))return v;m*=128;}throw Error('Invalid Snow Storm number');}
  n(max:number){const n=this.v();if(n>max)throw Error('Snow Storm count exceeds limit');return n;}
  s(){const n=this.n(4096);let s='';for(let i=0;i<n;i++)s+='%'+this.u().toString(16).padStart(2,'0');try{return decodeURIComponent(s);}catch{return '?';}}
}
class Writer {
  b:number[]=[17];
  u(n:number){this.b.push(n&255);return this;}
  v(n:number){n=Math.max(0,Math.floor(n));do{const b=n%128;n=Math.floor(n/128);this.u(b|(n?128:0));}while(n);return this;}
  s(s:string){const bytes=unescape(encodeURIComponent(s)).split('').map(c=>c.charCodeAt(0));this.v(bytes.length);this.b.push(...bytes);return this;}
  send(){nativeSend(new Uint8Array(this.b).buffer);}
}
const send=(op:number,id?:number)=>{const w=new Writer().u(op);if(id!==undefined)w.v(id);w.send();};
function begin(config:any,ids:number[],id:number,now:number){
  targetId='';
  engine=new Engine(data,config);rosterIds=ids;game=id;nextTurn=0;pending=[];acc=0;last=received=now;
  view.setEngine(engine);view.cam.zoom=view.cam.user=1;aim=0;chats=[];
}
function receive(buffer:ArrayBuffer,eid:number,now:number){
  me=eid;const r=new Reader(buffer),op=r.u();
  if(op===1){
    const list=[];
    for(let n=r.n(8);n--;){
      const g:any={id:r.v(),host:r.v(),name:r.s(),arena:r.u(),teams:r.u(),players:r.u(),minutes:r.u(),level:r.u(),fill:r.u(),running:!!r.u(),capacity:r.u(),fillIn:r.v(),roster:[]};
      for(let p=r.n(16);p--;)g.roster.push({id:r.v(),name:r.s(),team:r.u()});
      list.push(g);
    }
    games=list;
    // Our match is gone from the server (it ended while we were away, or its state was lost).
    // Only after a resync request, so a normal ending still shows the results.
    if(engine&&!local&&game&&engine.status!=='over'&&syncAt&&now-received>3000&&!list.some((g:any)=>g.id===game&&g.running)){
      engine=null;game=0;pending=[];view.setEngine(null);ended=now;
    }
  }else if(op===2){
    const id=r.v(),seed=r.v(),arena=r.u(),teams=r.u(),players=r.u(),minutes=r.u(),level=r.u(),roster=[],ids=[];
    for(let n=r.n(16);n--;){const eid=r.v(),name=r.s(),look=r.v(),team=r.u();ids.push(eid);roster.push({name,look,team,bot:eid===0});}
    if(![1,2,3,7].includes(arena)||teams<1||teams>4||players<1||players>8||![2,3,5].includes(minutes)||level>2||!roster.length)throw Error('Invalid Snow Storm match');
    local=false;begin({arena,teams,players,minutes,level,seed:seed>>>0,roster,me:ids.indexOf(me)},ids,id,now);
  }else if(op===3){
    const id=r.v(),turn=r.v(),commands=[];
    for(let n=r.n(64);n--;)commands.push({sub:r.u(),slot:r.u(),kind:r.u(),x:r.v(),y:r.v(),traj:r.u()});
    if(!engine||local||id!==game||turn<nextTurn)return;
    if(turn!==nextTurn){if(now-syncAt>2000){send(8);syncAt=now;}return;}
    if(pending.length>6000)throw Error('Snow Storm replay is too large');
    for(let sub=0;sub<SUBTURNS_PER_TURN;sub++)pending.push(commands.filter(c=>Math.min(c.sub,SUBTURNS_PER_TURN-1)===sub));
    nextTurn++;received=now;
  }else if(op===4){
    const id=r.v(),eid=r.v(),name=r.s(),text=r.s();
    if(id!==game&&!games.some(g=>g.id===id&&g.roster.some((p:any)=>p.id===me)))return;
    chats.push(`${name}: ${text}`);chats=chats.slice(-5);
    view.addChat(name,text,engine?.avatars[rosterIds.indexOf(eid)]||null,now);
  }
}
// Same sound choices as the browser client: every sound event, a walk loop for the local player, and the result jingle.
function play(events:any[],now:number){
  view.consume(events,now);
  const mine=engine?.me?.id;
  for(const ev of events){
    if(ev.type==='sound')nativeSound(ev.name,0);
    if(ev.type==='step'&&ev.id===mine)nativeSound('LS-walk-loop-1',1);
    if((ev.type==='stop'||ev.type==='throw'||ev.type==='stun')&&ev.id===mine)nativeStopWalk();
    if(ev.type==='finish'&&engine){nativeStopWalk();const r=engine.result();nativeSound(r.draw?'LS-C64-draw-1':r.won?'LS-C64-win-1':'LS-C64-loose-1',0);}
  }
}
function tick(now:number){
  if(!engine)return;
  const dt=Math.max(0,Math.min(250,now-last));last=now;
  if(local){localTurn+=dt;while(localTurn>=TURN_MS){for(let i=0;i<SUBTURNS_PER_TURN;i++)pending.push([]);localTurn-=TURN_MS;}}
  acc+=dt;
  const catchup=pending.length>60,interval=pending.length>15?40:60;
  let steps=0;
  while(pending.length&&(catchup||acc>=interval)&&steps++<(catchup?20:6)){
    engine.step(pending.shift()!);if(!catchup)acc-=interval;
    if(!catchup)play(engine.events.splice(0),now);else engine.events.length=0;
  }
  if(catchup){view.setEngine(engine);view.cam.zoom=view.cam.user=1;acc=0;}
  if(!pending.length)acc=Math.min(acc,60);
  if(!local&&engine.status!=='over'&&now-received>3000&&now-syncAt>3000){send(8);syncAt=now;}
}
function command(kind:number,x=0,y=0){
  if(!engine||engine.status!=='started')return;
  if(local){if(!pending.length)pending.push([]);pending[0].push({sub:0,slot:engine.config.me,kind,x,y,traj:trajectory});}
  else new Writer().u(6).v(game).u(kind).v(x).v(y).u(trajectory).send();
}
// Targets: the chosen rival while it is in the game, otherwise the nearest one (auto-aim).
let targetId='';
function enemies(){
  const e=engine;if(!e)return [];
  const me=e.me;
  const list=e.avatars.filter(a=>a!==me&&(e.ffa||a.team!==me?.team));
  const d=(a:any)=>me?(a.x-me.x)**2+(a.y-me.y)**2:0;
  return list.sort((a,b)=>d(a)-d(b));
}
function target(){
  const list=enemies();
  return list.find(a=>a.id===targetId)||list[0]||null;
}
function status(now:number){
  const mine=games.find(g=>g.roster.some((p:any)=>p.id===me));
  const e=engine;
  return JSON.stringify({ended:ended&&now-ended<15000,settings,games,waiting:!e?mine:null,chats,game,local,trajectory,target:target()?.name||'',manual:!!e&&e.avatars.some(a=>a.id===targetId),
    mode:e?(e.status==='over'?'results':'game'):mine?'waiting':'list',
    syncing:!!e&&!local&&now-received>1500,queued:pending.length,
    state:e?{status:e.status,clock:e.clock,left:Math.max(0,Math.ceil((e.endAt-e.clock)/1000)),hp:e.me?.hp||0,balls:e.me?.balls||0,
      avatars:e.avatars.map(a=>({name:a.name,team:a.team,score:a.score,hp:a.hp,balls:a.balls,point:view.worldScreen(a.x,a.y,0),me:a===e.me,target:a===target(),enemy:a!==e.me&&(e.ffa||a.team!==e.me?.team),chosen:a.id===targetId})),
      result:e.status==='over'?e.result():null}:null});
}
const Snow={
  open(eid:number){me=eid;send(1);send(8);},receive,tick,status,
  draw(now:number){view.hoverAvatar=target();view.draw(now,settings.arena);},
  list(){send(1);},join(id:number){send(3,id);},start(id:number){send(5,id);},
  create(arena:number,teams:number,players:number,minutes:number,level:number,fill:number){
    settings={arena,teams,players,minutes,level,fill};new Writer().u(2).u(arena).u(teams).u(players).u(minutes).u(level).u(fill).send();
  },
  practice(eid:number,look:number,name:string,now:number){
    me=eid;local=true;localTurn=0;
    const roster=Array.from({length:settings.teams*settings.players},(_,i)=>({name:i?`Snowbot ${i}`:name,look,team:1+Math.floor(i/settings.players),bot:i!==0}));
    begin({...settings,seed:123456789,roster,me:0},[me],0,now);
  },
  leave(){nativeStopWalk();if(!local)send(4);engine=null;game=0;pending=[];view.setEngine(null);local=false;send(1);},
  chat(text:string){if(local){view.addChat(engine!.me!.name,text,engine!.me,last);return;}const g=game||games.find(g=>g.roster.some((p:any)=>p.id===me))?.id;if(g)new Writer().u(7).v(g).s(text).send();},
  move(dx:number,dy:number){const m=engine?.me;if(!m)return;const [x,y]=engine!.tileOf(m);command(1,Math.max(0,x+dx),Math.max(0,y+dy));},
  touch(x:number,y:number,throwing:number){const t=view.tileAt(x,y);command(throwing?3:1,Math.max(0,t[0])*(throwing?TILE:1),Math.max(0,t[1])*(throwing?TILE:1));},
  make(){command(2);},throw(){const a=target(),m=engine?.me;if(a&&m){const [x,y]=leadPoint(m,a);command(3,x,y);}},
  aim(){const list=enemies();if(!list.length)return;const at=list.findIndex(a=>a.id===target()?.id);targetId=list[(at+1)%list.length].id;},
  pick(index:number){const a=engine?.avatars[index];if(a&&a!==engine!.me)targetId=targetId===a.id?'':a.id;},trajectory(){trajectory=(trajectory+1)%3;},
  // Integer zoom preserves the imported pixel grid on the 3DS screen.
  zoom(){view.cam.zoom=view.cam.user=view.cam.zoom===1?2:1;},
};
(globalThis as any).Snow=Snow;
(globalThis as any).SnowTest={Engine,data,assets,View};
