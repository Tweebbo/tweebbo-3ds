(function(){
  let draws=0,heads=0,sent=[];
  function check(ok,message){if(!ok)throw Error(message);}
  globalThis.nativeHead=function(){heads++;for(let i=0;i<arguments.length;i++)check(Number.isFinite(arguments[i]),'Invalid avatar coordinate');};
  nativeSprite=function(){draws++;for(let i=0;i<arguments.length;i++)check(Number.isFinite(arguments[i]),'Invalid sprite coordinate');};
  globalThis.nativeSendText=function(raw){sent.push(JSON.parse(raw));};
  globalThis.nativeDisconnect=function(){};
  const results=[];
  for(const goal of [6,12,18]){
    FastFood.open();FastFood.practice('Native',0,goal,0);
    for(let tick=0;tick<30000;tick++){
      FastFood.tick(tick*16);
      if(tick%30===0){FastFood.draw();const state=JSON.parse(FastFood.status()).state;
        if(state.phase==='results'){results.push([goal,state.tick,state.winner,state.players.map(p=>p.score)]);break;}
        const p=state.players[0];if(!p.plate&&!p.cooldown)FastFood.action('drop');
        else if(p.plate?.state==='fall'&&p.plate.y<.6)FastFood.action('parachute');
        if(tick%180===0)FastFood.action('missile');
      }
    }
    FastFood.leave();
  }
  check(results.length===3,'A native practice match did not finish');
  const state=new FastFoodTest.Engine(1,'Network',false,6);state.players.length=2;
  FastFood.receive(JSON.stringify({type:'state',slot:1,game:1,state}),0);FastFood.action('drop');
  check(sent[sent.length-1].op==='action','Online input was simulated locally');
  for(const dimensions of [[400,240],[480,272],[640,360]]){FastFood.resize(...dimensions);FastFood.draw();}
  FastFood.receive('{"type":"connection","connected":false}',1);
  check(JSON.parse(FastFood.status()).state===null,'Disconnected match stayed on screen');
  const before=sent.length;FastFood.action('drop');check(sent.length===before,'Disconnected input was queued');
  FastFood.receive('{"type":"error","message":"Opened in another tab.","terminal":true}',2);
  FastFood.receive('{"type":"connection","connected":false}',3);
  check(JSON.parse(FastFood.status()).error==='Opened in another tab.','Terminal error was hidden');
  FastFood.practice('Native',0,6,4);check(JSON.parse(FastFood.status()).lobby===null,'Practice retained online lobby');
  FastFood.leave();check(JSON.parse(FastFood.status()).state===null,'Practice did not close');
  let rejected=false;try{FastFood.receive(JSON.stringify({type:'state',slot:9,state}),5);}catch{rejected=true;}
  check(rejected,'Invalid player slot accepted');
  check(draws>1000&&heads>100,'Original renderer did not emit native art and avatars');
  print(JSON.stringify({results,draws,heads}));
})();
