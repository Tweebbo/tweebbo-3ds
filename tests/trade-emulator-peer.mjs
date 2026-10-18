import assert from 'node:assert/strict';
const base=process.env.FIXTURE_SERVER||'ws://127.0.0.1:18092';
assert.equal(base,'ws://127.0.0.1:18092','Use the isolated emulator fixture server.');
const variable=n=>{const b=[];do{b.push(n%128|(n>=128?128:0));n=Math.floor(n/128);}while(n);return b;};
const string=s=>{const b=Buffer.from(s);return[...variable(b.length),...b];};
class Reader{
 constructor(b){this.b=b;this.p=1;}
 u(){return this.b[this.p++];}v(){let n=0,m=1,k;do{k=this.u();n+=(k&127)*m;m*=128;}while(k&128);return n;}
 s(){const n=this.v(),s=this.b.subarray(this.p,this.p+n).toString();this.p+=n;return s;}
}
let native=0,item=0,invited=false,added=false,ready=false,confirmed=false,complete=false;
const ws=new WebSocket(base+'/ws/lobby');ws.binaryType='arraybuffer';
const send=b=>ws.send(new Uint8Array(b)),trade=o=>send([21,...string(JSON.stringify(o))]);
function invite(){if(native&&item&&!invited){invited=true;trade({action:'invite',target:native});}}
function ent(r){const id=r.v();r.s();r.u();r.u();r.u();const flags=r.u();r.v();r.u();r.u();r.v();if(flags&64)native=id;}
ws.onopen=()=>send([1,6,0,0,0,0,0,5]);
ws.onmessage=e=>{
 const b=Buffer.from(e.data),r=new Reader(b);
 if(b[0]===101)send([6,1,0]);
 if(b[0]===102){r.s();r.s();r.s();r.u();r.u();r.s();r.u();r.u();r.v();r.s();for(let n=r.v();n--;){r.v();r.u();r.u();r.u();r.u();r.u();r.u();r.v();r.u();r.u();}for(let n=r.v();n--;)ent(r);}
 if(b[0]===103)ent(r);
 if(b[0]===114)item=r.v();
 if(b[0]===139){
  const state=JSON.parse(r.s());
  if(state.stage==='open'){
   if(!added){added=true;trade({action:'add',id:item});}
   else if(state.mine.length&&!state.confirming&&!ready){ready=true;trade({action:'ready',revision:state.revision});}
   else if(state.confirming&&!confirmed){confirmed=true;trade({action:'confirm',revision:state.revision});}
  }
  if(state.stage==='done'){complete=true;console.log('Native/browser trade completed after acceptance and both final confirmations.');ws.close();}
 }
 invite();
};
await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{ws.close();reject(Error('Emulator trade did not complete'));},90000);ws.onclose=()=>{clearTimeout(timer);complete?resolve():reject(Error('Trade peer closed before completion'));};ws.onerror=reject;});
