import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
const variable=n=>{const a=[];do{a.push((n&127)|(n>127?128:0));n>>>=7;}while(n);return a;};
const string=s=>{const b=Buffer.from(s);return [...variable(b.length),...b];};
const previewMode={capacity:2,locked:1,moderation:3}[process.env.PREVIEW];
const preview=previewMode!==undefined;
for(const mode of preview?[previewMode]:[0,1,2,3]){
 const sockets=new Set();let connections=0;
 const server=createServer();
 server.on('upgrade',(req,socket)=>{
  sockets.add(socket);socket.on('error',()=>{});socket.on('close',()=>sockets.delete(socket));connections++;
  const accept=createHash('sha1').update(req.headers['sec-websocket-key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  let pending=Buffer.alloc(0);
  const send=a=>{const b=Buffer.from(a);assert(b.length<126);socket.write(Buffer.concat([Buffer.from([0x82,b.length]),b]));};
  socket.on('data',chunk=>{
   pending=Buffer.concat([pending,chunk]);if(pending.length<6)return;
   const length=pending[1]&127;if(length>=126)return;
   if(pending.length<length+6)return;
   const mask=pending.subarray(2,6),b=Buffer.from(pending.subarray(6,6+length));
   for(let i=0;i<b.length;i++)b[i]^=mask[i%4];pending=pending.subarray(6+length);
   if(b[0]!==1)return;
   let p=3;for(let i=0;i<2;i++){const n=b[p++];p+=n;}
   assert.equal(b[p++],1,'native HELLO must retain the console platform byte');
   const size=b[p++],password=b.subarray(p,p+size).toString();p+=size;assert.equal(b[p]&6,6);
   if(mode===1&&password==='correct'){send([101,1,...string('Fixture'),...string('fixture-identity'),200,1,1]);return;}
   send([113,...string('The server has not admitted this connection.')]);
   if(mode===1)send([129,1]);if(mode===2)send([132,1,preview?90:30]);if(mode===3)send([133,...string('Paused by a moderator.')]);
   setTimeout(()=>socket.end(),100);
  });
 });
 server.listen(preview?18094:0,'127.0.0.1');await once(server,'listening');
 if(preview){console.log('Isolated status preview on ws://127.0.0.1:18094');await new Promise(()=>{});}
 const child=spawn(new URL('./transport_test',import.meta.url).pathname,[`ws://127.0.0.1:${server.address().port}`,String(mode)],{stdio:['ignore','pipe','pipe']});
 let output='';child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);
 const [code]=await once(child,'exit');for(const socket of sockets)socket.destroy();await new Promise(r=>server.close(r));
 assert.equal(code,0,`Transport mode ${mode}: ${output}`);assert.equal(connections,mode===1?2:1);
}
console.log('Native transport: notices are not welcome; password retry, capacity cooldown and moderation pause passed.');
