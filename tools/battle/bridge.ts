import data from 'battle-lookup';
import {BattleCamera} from 'battle-original/battle-camera';
import {BattleTiles} from 'battle-original/battle-tiles';
import {bounceOffset,environmentMember,sequenceMember,pinMember,POWER_ART,TEAM_COLORS,TIMING} from 'battle-original/battle-animation';
import type {Battle as Game} from 'battle-original/activities-state';
declare const nativeSprite:(...args:number[])=>void;
declare const nativeAvatar:(...args:number[])=>void;
declare const nativeBlend:(mode:number)=>void;
declare const nativeSendText:(json:string)=>void;
declare const nativeSound:(name:string,channel:number,loop:boolean)=>void;
const maps:any=data.maps,banks:any=data.banks;
const camera=new BattleCamera(),tiles=new BattleTiles();
let game:Game|null=null,previous:Game|null=null,received=0,offset=0,me=0,arena=5,width=400,height=240;
let scale=1,ox=0,oy=0;
let heard=0,match=0,phase=-1;
const rgb=(color:string)=>parseInt(color.slice(1),16);
function art(name:string,x:number,y:number,color=0xffffff,alpha=1,flip=false){
  const s=banks[arena][name];if(!s)return;
  const left=flip?x+s.reg[0]:x-s.reg[0];
  nativeSprite(s.id,Math.round(ox+left*scale),Math.round(oy+(y-s.reg[1])*scale),flip?-scale:scale,scale,color,alpha,0,0,width,height);
}
function send(op:string,fields:Record<string,unknown>={}){nativeSendText(JSON.stringify({op,...fields}));}
function configure(){
  const map=maps[arena],g=map.geometry, floor=map.elements.find((e:any)=>e.id==='floor'),sprite=floor&&banks[arena][String(floor.member)];
  const left=sprite?Number(floor.locH)+map.viewport[0]-g.offsetx-sprite.reg[0]:-360;
  const top=sprite?Number(floor.locV)+map.viewport[1]-g.offsety-sprite.reg[1]:-244;
  camera.configure({left,top,right:left+(sprite?.w??720),bottom:top+(sprite?.h??488)},width,height);
}
const Battle={
  open(id:number){me=id;game=previous=null;arena=5;heard=match=0;phase=-1;tiles.receive(null,0,0);configure();camera.centre();},
  receive(raw:string,now:number){
    const packet=JSON.parse(raw);if(!packet.state||!('battle' in packet.state))return;
    const next=packet.state.battle as Game|null;
    if(next&&(!maps[next.arena]||!Array.isArray(next.players)||next.players.length>16||!Array.isArray(next.tiles)||next.tiles.length>10000))throw Error('Invalid Battle Ball state');
    previous=game;game=next;received=now;offset=packet.now-now;
    if(game){if(arena!==game.arena){arena=game.arena;configure();camera.centre();}tiles.receive(game,Math.max(...maps[arena].rows.map((r:string)=>r.length)),packet.now);}
    else tiles.receive(null,0,packet.now);
  },
  preview(id:number){if(!game&&maps[id]){arena=id;configure();camera.centre();}},
  resize(w:number,h:number){width=w;height=h;configure();},
  arena(){return arena;},
  move(dx:number,dy:number){const p=game?.players.find(p=>p.eid===me);if(p)send('battle_move',{x:p.x+dx,y:p.y+dy});},
  tap(x:number,y:number){
    if(!game||game.phase!==1)return;const [wx,wy]=camera.world(x,y),map=maps[arena];let best:number[]|null=null,distance=Infinity;
    for(let ty=0;ty<map.rows.length;ty++)for(let tx=0;tx<map.rows[ty].length;tx++){
      if(!/\d/.test(map.rows[ty][tx]))continue;
      const sx=(tx-ty)*16+16,sy=(tx+ty)*8-Number(map.rows[ty][tx])*map.geometry.factorh,d=Math.abs(wx-sx)/16+Math.abs(wy-sy)/8;
      if(d<distance){distance=d;best=[tx,ty];}
    }
    if(best&&distance<1.5)send('battle_move',{x:best[0],y:best[1]});
  },
  zoom(delta:number){camera.zoomBy(delta);},
  centre(){camera.centre();},
  audio(localNow:number){
    if(!game){nativeSound('',3,false);phase=-1;return;}
    if(game.starts!==match){match=game.starts;heard=0;}
    if(game.phase!==phase){phase=game.phase;if(phase!==1)nativeSound('',3,false);if(phase===2)nativeSound(game.winner===game.players.find(p=>p.eid===me)?.team?'LS-C64-win-1.flac':'LS-C64-loose-1.flac',-1,false);}
    if(phase===1)nativeSound('BB2-musicloop.flac',3,true);
    const now=localNow+offset;
    for(const event of game.events??[]){if(event.id<=heard)continue;heard=event.id;if(event.kind==='fill'||now-event.at>=1000)continue;nativeSound(event.kind==='pickup'?'SFX-18-poweruppickup.flac':event.kind==='burst'?'SFX-10-stunned.flac':`SFX-${event.power}.flac`,-1,false);}
  },
  draw(localNow:number){
    const now=localNow+offset,map=maps[arena],g=map.geometry;
    const followed=game?.players.find(p=>p.eid===me);
    if(followed&&game?.phase===1)camera.track((followed.x-followed.y)*16+16,(followed.x+followed.y)*8-Number(map.rows[followed.y][followed.x])*g.factorh-24);
    scale=camera.scale;[ox,oy]=camera.origin;
    for(const e of map.elements.filter((e:any)=>e.member&&e.blend!==0&&e.type!=='hiliter'&&e.ink!==7).sort((a:any,b:any)=>Number(a.locZ??-100000)-Number(b.locZ??-100000))){
      art(environmentMember(e,now,banks[arena]),Number(e.locH)+map.viewport[0]-g.offsetx,Number(e.locV)+map.viewport[1]-g.offsety,0xffffff,Number(e.blend??100)/100);
    }
    if(!game||game.phase!==1)return;
    const tw=Math.max(...map.rows.map((r:string)=>r.length));
    for(let y=0;y<map.rows.length;y++)for(let x=0;x<map.rows[y].length;x++){
      if(map.tiles[y]?.[x]!=='1'&&!(arena===1&&x===24&&(y===17||y===18)))continue;
      const actual=game.tiles[y*tw+x],tile=tiles.paint(y*tw+x,actual?[actual[0],actual[1]]:undefined,now);
      if(tile&&tile[1])art(`tile${tile[0]+1}_${tile[1]}`,(x-y)*16,(x+y)*8-Number(map.rows[y][x])*g.factorh-2);
    }
    for(const p of game.powers){const member=sequenceMember(`s_bb2_pu_${POWER_ART[p.kind]}_a_0_1_1_0_`,now-(p.expires-15000),true,TIMING.objectFrameMs);if(member)art(member,(p.x-p.y)*16,(p.x+p.y)*8-Number(map.rows[p.y][p.x])*g.factorh);}
    for(const p of game.pins)for(const layer of ['a','b'] as const)art(pinMember(layer,now-(p.expires-15000),p.x,p.y),(p.x-p.y)*16,(p.x+p.y)*8-Number(map.rows[p.y][p.x])*g.factorh);
    const elapsed=Math.min(1,Math.max(0,(localNow-received)/TIMING.movementMs));
    game.players.map((p,i)=>({p,i})).sort((a,b)=>a.p.x+a.p.y-b.p.x-b.p.y).forEach(({p,i})=>{
      const old=previous?.starts===game!.starts?previous.players[i]:undefined,continuous=old&&Math.abs(old.x-p.x)<=1&&Math.abs(old.y-p.y)<=1;
      const cannon=(game!.events??[]).find(e=>e.slot===i&&e.kind==='activate'&&e.power===4&&now-e.at<800);
      const flight=cannon?Math.min(1,Math.max(0,(now-cannon.at)/TIMING.movementMs)):1;
      const x=cannon?cannon.x+(p.x-cannon.x)*flight:continuous?old.x+(p.x-old.x)*elapsed:p.x,y=cannon?cannon.y+(p.y-cannon.y)*flight:continuous?old.y+(p.y-old.y)*elapsed:p.y;
      const moving=!!continuous&&(old.x!==p.x||old.y!==p.y)&&elapsed<1,age=moving?elapsed*TIMING.movementMs:now-game!.starts+i*43,jump=-bounceOffset(p,moving,age,now);
      const px=(x-y)*16+16,py=(x+y)*8-Number(map.rows[p.y]?.[p.x]??0)*g.factorh;
      const team=p.coloring_for!=null?game!.players[p.coloring_for]?.team??p.team:p.team;
      if((p.broken_until??0)<=now)art('sh_wlk_bl_001_1_0',px-16+[0,-2,-2,-2,-2,-2,-2,-1][p.dir],py-jump,rgb(TEAM_COLORS[team]));
      nativeAvatar(p.look,p.dir,ox+(px-16)*scale,oy+(py-49-jump)*scale,.5*scale);
      const effect=(prefix:string,age:number,loop=true,frameMs=TIMING.effectFrameMs,blend=1,dy=0)=>{const member=sequenceMember(prefix,age,loop,frameMs);if(member){nativeBlend(blend);art(member,px-16,py+dy);nativeBlend(0);}};
      if(p.stunned>now&&!cannon)effect('bb2_stunned_0_',now-game!.starts,true,TIMING.effectFrameMs,0,(p.broken_until??0)>now?13:0);
      if((p.broken_until??0)>now)effect('bb2_efct_pu_harlequin_0_',now-(p.broken_until!-3000),false);
      if(cannon)effect(`bb2_efct_pu_cannon_${p.dir}_`,now-cannon.at);
      if(moving&&p.effect===2)effect('bb2_efct_pu_spring_0_',age-TIMING.effectStartMs,false);
      if(moving&&p.effect===8)effect('bb2_efct_pu_drill_0_',age-TIMING.effectStartMs,false);
      if(p.effect===6)effect('bb2_efct_pu_harlequin_0_',now-(p.until-10000),false);
      for(const event of game!.events??[]){if(event.slot!==i||event.kind!=='activate')continue;
        if(event.power===1||event.power===7)effect('bb2_efct_pu_lghtbulb_0_',now-event.at,false,TIMING.effectFrameMs,event.power===7?2:1);
        else if(event.power===3)effect(`bb2_efct_pu_flashlght_${event.dir}_`,now-event.at,false,TIMING.slowEffectFrameMs);
        else effect('bb2_pickup_0_',now-event.at,false);
      }
      if(p.eid===me&&now<game!.starts)art(`puppet_hilite_sh_${[0,1,2,3,2,1,0,3][p.dir]}`,px,py-jump-58,0xffffff,1,p.dir>=4);
    });
  }
};
(globalThis as any).Battle=Battle;
