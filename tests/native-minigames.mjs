import assert from 'node:assert/strict';
import fs from 'node:fs';
const base=process.env.FIXTURE_SERVER||'ws://127.0.0.1:18093';
assert.equal(new URL(base).hostname,'127.0.0.1');assert(['18092','18093'].includes(new URL(base).port),'Use an isolated native fixture server.');
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const variable=n=>{const b=[];do{const v=n%128;n=Math.floor(n/128);b.push(v|(n?128:0));}while(n);return b;};
class Reader{
  constructor(b){this.b=b;this.p=1;}
  u(){assert(this.p<this.b.length);return this.b[this.p++];}
  u16(){return this.u()|(this.u()<<8);}
  v(){let n=0,m=1,b;do{b=this.u();n+=(b&127)*m;m*=128;}while(b&128);return n;}
  s(){const n=this.v();assert(this.p+n<=this.b.length);const s=this.b.subarray(this.p,this.p+n).toString();this.p+=n;return s;}
}
class Peer{
  constructor(room,caps=62){
    this.events=[];this.credits=0;this.items=[];this.snow=[];
    this.ws=new WebSocket(`${base}/ws/${room}`);this.ws.binaryType='arraybuffer';
    this.ws.onopen=()=>this.send([1,6,0,0,0,caps?1:0,0,caps||5,...(caps?[1,1]:[])]);
    this.ws.onmessage=e=>{
      const b=Buffer.from(e.data),r=new Reader(b);
      if(b[0]===101){this.id=r.v();r.s();r.s();this.credits=r.v();}
      if(b[0]===109)this.credits=r.v();
      if(b[0]===113)this.events.push({t:'notice',text:r.s()});
      if(b[0]===131)this.events.push(JSON.parse(r.s()));
      if(b[0]===128)this.snow.push(b);
      if(b[0]===102){
        r.s();r.s();r.s();r.u();r.u();r.s();r.u();r.u();r.v();r.s();
        this.items=[];for(let n=r.v();n--;){const it={id:r.v(),def:r.u16(),x:r.u(),y:r.u()};r.u();r.u();r.v();r.u16();this.items.push(it);}this.room=true;
      }
    };
  }
  send(b){this.ws.send(new Uint8Array(b));}
  async wait(fn,label,ms=30000){const until=Date.now()+ms;while(Date.now()<until){const v=fn();if(v)return v;await sleep(50);}throw Error(`Timeout: ${label}; ${JSON.stringify(this.events.filter(e=>e.t==='notice'))}`);}
  async event(type,action){const at=this.events.length;action();return this.wait(()=>this.events.slice(at).find(e=>e.t===type),type);}
}
const peers=[];
try{
  const p=new Peer('casino');peers.push(p);await p.wait(()=>p.room,'casino room');
  const catalogue=JSON.parse(fs.readFileSync(new URL('../build/export/data/models/catalogue.json',import.meta.url),'utf8'));
  for(const kind of ['slot','roulette','blackjack','poker','videopoker','crash','dash']){
    const it=p.items.find(it=>catalogue.definitions[it.def]?.flags.includes(`casino_${kind}`));assert(it,kind);
    const open=await p.event('open',()=>p.send([10,...variable(it.id)]));assert.equal(open.game,kind);
    if(kind==='slot'){
      for(let game=0;game<5;game++){
        const before=p.credits;const v=await p.event('slot',()=>p.send([19,2,1,game]));await sleep(750);
        assert.equal(p.credits,before-(v.free?0:1)+v.win);
        // Free spins are tied to their awarded game; leave them to the normal client.
        if(v.freeLeft)break;
      }
    }
    if(kind==='roulette')await p.event('roulette',()=>p.send([19,3,1,0,1]));
    if(kind==='blackjack'){
      const v=await p.event('bj',()=>p.send([19,4,1]));await sleep(750);
      if(!v.hand.result)await p.event('bj',()=>p.send([19,6]));
    }
    if(kind==='poker'){
      await p.event('poker',()=>p.send([19,11,1]));await sleep(750);
      const v=await p.event('poker',()=>p.send([19,12]));assert(v.hand.result);
    }
    if(kind==='videopoker'){
      await p.event('vp',()=>p.send([19,14,1]));await sleep(750);
      const v=await p.event('vp',()=>p.send([19,15,3]));assert(v.done);assert.equal(v.cards.length,5);
    }
    if(kind==='crash')assert(open.round.phase);
    if(kind==='dash'){
      const before=p.credits,stake=open.bets[0];
      const start=await p.event('dash',()=>p.send([19,16,0,0]));assert.equal(start.step,0);assert.equal(start.stake,stake);
      await p.wait(()=>p.credits===before-stake,'Dash debit');await sleep(750);
      const step=await p.event('dash',()=>p.send([19,17]));
      if(!step.done){
        await sleep(750);const out=await p.event('dash',()=>p.send([19,18]));assert(out.done&&!out.lost);assert(out.win>0);
        await p.wait(()=>p.credits===before-stake+out.win,'Dash cashout');
      }else{assert(step.lost);assert.equal(p.credits,before-stake);}
    }
    p.send([19,10]);await sleep(750);console.log(`Native Casino ${kind}: server-confirmed.`);
  }
  const a=new Peer('lobby'),b=new Peer('lobby',0);peers.push(a,b);await a.wait(()=>a.room&&b.room,'snow peers');
  a.send([17,2,1,2,1,2,1,0]);
  const list=await a.wait(()=>a.snow.find(p=>p[1]===1&&p[2]>0),'created Snow game');
  const rd=new Reader(list);rd.u();rd.v();const id=rd.v();
  b.send([17,3,...variable(id)]);
  await a.wait(()=>a.snow.some(p=>p[1]===2)&&b.snow.some(p=>p[1]===2),'crossplay start');
  assert.deepEqual(a.snow.find(p=>p[1]===2),b.snow.find(p=>p[1]===2));
  a.send([17,6,...variable(id),2,0,0,0]);b.send([17,6,...variable(id),1,15,15,0]);
  await a.wait(()=>a.snow.filter(p=>p[1]===3).length>=12&&b.snow.filter(p=>p[1]===3).length>=12,'crossplay turns');
  const turns=p=>p.snow.filter(p=>p[1]===3).slice(0,12).map(p=>p.toString('hex'));
  assert.deepEqual(turns(a),turns(b));a.send([17,8]);
  await a.wait(()=>a.snow.filter(p=>p[1]===2).length>=2,'Snow reconnect replay');
  a.send([17,4]);b.send([17,4]);
  console.log('Native/browser Snow Storm: identical roster and 12 turns; replay accepted.');
}finally{for(const p of peers)p.ws.close();}
