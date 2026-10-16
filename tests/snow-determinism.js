(() => {
  const {Engine,data,View,assets}=SnowTest;
  const results=[];
  const state=e=>JSON.stringify({clock:e.clock,status:e.status,avatars:e.avatars.map(({collecting,...a})=>({...a,collecting:collecting?{id:collecting.machine.id,give:collecting.give,add:collecting.add}:null})),balls:e.balls,machines:e.machines});
  for(const arena of [1,2,3,7])for(const level of [0,1,2]){
    const roster=Array.from({length:8},(_,i)=>({name:`Player ${i}`,look:4294967296+i*1734,team:i<4?1:2,bot:i!==0}));
    const e=new Engine(data,{arena,teams:2,players:4,minutes:2,level,seed:0xfedcba98,roster,me:0});
    const v=new View(assets);v.canvas.live=true;v.resize(400,240);v.setEngine(e);v.cam.zoom=v.cam.user=1;
    let digest=2166136261,hits=0;
    while(e.status!=='over'){
      const cmds=[];
      if(e.clock%900===0){const a=e.avatars[4];cmds.push({sub:0,slot:0,kind:3,x:a.x,y:a.y,traj:(e.clock/900)%3});}
      if(e.clock%1800===0)cmds.push({sub:0,slot:0,kind:2,x:0,y:0,traj:0});
      e.step(cmds);hits+=e.events.filter(e=>e.type==='hit').length;
      const before=state(e);v.consume(e.events.splice(0),e.clock);if(e.clock%300===0)v.draw(e.clock);
      if(state(e)!==before)throw Error('View mutated deterministic state');
      if(e.clock%600===0)for(let i=0;i<before.length;i++)digest=Math.imul(digest^before.charCodeAt(i),16777619)>>>0;
    }
    results.push({arena,level,digest,hits,score:e.avatars.map(a=>a.score),result:e.result()});
  }
  print(JSON.stringify(results));
})();
