// Browser-protocol chess peer for tools/emu/steps-game; isolated test hotel only.
import assert from 'node:assert/strict';
const base = process.env.FIXTURE_SERVER || 'ws://127.0.0.1:18093';
assert.equal(new URL(base).hostname, '127.0.0.1');
const targetGuest = Number(process.env.FIXTURE_GUEST || 2);
const variable = n => { const a=[]; do { a.push((n&127)|(n>127?128:0)); n>>>=7; } while(n); return a; };
class Reader {
  constructor(b) { this.b=b; this.i=1; }
  u8() { return this.b[this.i++]; }
  u16() { return this.u8() | this.u8()<<8; }
  var() { let n=0,s=0,b; do { b=this.u8(); n+=(b&127)*2**s; s+=7; } while(b&128); return n; }
  str() { const n=this.var(), v=this.b.subarray(this.i,this.i+n).toString(); this.i+=n; return v; }
}
const ws = new WebSocket(base+'/ws/lobby'); ws.binaryType='arraybuffer';
let own=0, challenged=false;
const send = b => ws.send(new Uint8Array(b));
const entity = r => {
  const id=r.var(); r.str(); r.u8(); r.u8(); r.u8(); const flags=r.u8(); r.var(); r.u16(); const guest=r.var();
  if(guest===targetGuest && (flags&64) && !challenged) {
    challenged=true;
    setTimeout(()=>{ if(ws.readyState===1) { send([16,1,...variable(id),3]); console.log('Chess invitation sent to the isolated console fixture.'); } },18000);
  }
};
ws.onopen=()=>send([1,6,0,0,0,0,0,2]);
ws.onmessage=e=>{
  const b=Buffer.from(e.data),r=new Reader(b);
  if(b[0]===101) own=r.var();
  if(b[0]===103) entity(r);
  if(b[0]===102) {
    r.str();r.str();r.str();r.u8();r.u8();r.str();r.u8();r.u8();r.var();r.str();
    for(let n=r.var();n;n--) {r.var();r.u16();r.u8();r.u8();r.u8();r.u8();r.var();r.u16();}
    for(let n=r.var();n;n--) entity(r);
  }
  if(b[0]===127 && r.u8()===2) {
    const id=r.var(); r.u8(); const players=[r.var(),r.var()], turn=r.u8(),result=r.u8();
    r.u8();r.u8();r.u8();r.u8(); for(let n=r.var();n;n--)r.u8();
    const count=r.var();
    if(count && players[turn]===own && !result) { const from=r.u8(),to=r.u8(); setTimeout(()=>send([16,3,...variable(id),from,to,5]),700); }
    if(result) console.log('Chess finished with a server-confirmed result.');
  }
};
await new Promise(resolve=>setTimeout(resolve,110000));
ws.close();
