import {createRequire} from 'node:module';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import assert from 'node:assert/strict';
const require=createRequire(new URL('../../Tweebbo-Vercel/client/package.json',import.meta.url));
const {wsServer:WebSocketServer}=require('../../Tweebbo-Vercel/client/node_modules/playwright-core/lib/utilsBundle.js');
const server=new WebSocketServer({host:'127.0.0.1',port:0});
await once(server,'listening');
const str=s=>[Buffer.byteLength(s),...Buffer.from(s)];
server.on('connection',ws=>ws.on('message',b=>{
  if(b[0]===1)ws.send(Buffer.from([101,1,...str('Fixture'),...str('fixture-token'),100,1,1]));
  if(b[0]===3)ws.send(Buffer.from([106,1,...b.subarray(1)]));
}));
try {
  const child=spawn(process.argv[2]||'./input-latency',[`ws://127.0.0.1:${server.address().port}`],{stdio:'inherit'});
  const [code]=await once(child,'exit');assert.equal(code,0);
} finally { for(const ws of server.clients)ws.terminate();await new Promise(r=>server.close(r)); }
