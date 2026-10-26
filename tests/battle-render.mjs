import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
const data=JSON.parse(fs.readFileSync('../build/battle/lookup.json','utf8'));
const fixtures=[];
for(let arena=1;arena<=5;arena++){
 const map=data.maps[arena],width=Math.max(...map.rows.map(r=>r.length));
 const players=Array.from({length:4},(_,team)=>{const p=map.spawns.find(p=>p.team_id===team);return {eid:team+1,name:'Test '+team,look:0,team,x:p.x,y:p.y,dir:p.rotation,score:0,powers:[team+1],effect:2,until:20000,stunned:0,path:[]};});
 const tiles=Array.from({length:width*map.rows.length},(_,i)=>[i%4,1+i%3,[]]);
 fixtures.push({arena,players,tiles,teams:4,minutes:2,host:1,phase:1,starts:1000,ends:121000,tick:0,powers:players.map((p,i)=>({x:p.x,y:p.y,kind:i+1,expires:16000})),pins:[],winner:null,events:[]});
}
const script=`
let draws=0,avatars=0,commands=[];
globalThis.nativeAvatar=function(){avatars++;for(const n of arguments)if(!Number.isFinite(n))throw Error('Invalid avatar coordinate');};
globalThis.nativeSprite=function(){draws++;for(const n of arguments)if(!Number.isFinite(n))throw Error('Invalid sprite coordinate');};
globalThis.nativeBlend=function(mode){if(![0,1,2].includes(mode))throw Error('Invalid blend');};
globalThis.nativeSendText=function(json){commands.push(JSON.parse(json));};
for(const game of ${JSON.stringify(fixtures)}){
 Battle.open(1);Battle.receive(JSON.stringify({now:1000,state:{battle:game}}),1000);
 for(const size of [[400,240],[480,272],[640,360]]){
  Battle.resize(...size);
  for(let frame=0;frame<16;frame++){
   game.events=Array.from({length:8},(_,i)=>({id:i+1,at:1000,slot:i%4,kind:'activate',power:i+1,x:game.players[i%4].x,y:game.players[i%4].y,dir:i}));
   Battle.receive(JSON.stringify({now:1000+frame*30,state:{battle:game}}),1000+frame*30);
   Battle.draw(1000+frame*30);
  }
  Battle.zoom(1);Battle.draw(1600);Battle.centre();Battle.move(1,0);
 }
}
if(draws<1000||avatars<100||commands.length!==15)throw Error('Incomplete native Battle Ball render');
print(JSON.stringify({arenas:5,viewports:3,draws,avatars,commands:commands.length}));
`;
fs.mkdirSync('../build/tests',{recursive:true});fs.writeFileSync('../build/tests/battle-render.js',script);
const result=spawnSync('./snow_host',['../romfs/battle/game.js','../build/tests/battle-render.js'],{env:{...process.env,QUICKJS_TEST_HEAP_MB:'4'},encoding:'utf8'});
assert.equal(result.status,0,result.stderr);console.log(result.stdout.trim());console.log(result.stderr.trim());
