(function(){
  function check(ok,message){if(!ok)throw Error(message);}
  let sprites=0,sent=[];
  nativeSprite=function(){sprites++;for(let i=0;i<arguments.length;i++)check(Number.isFinite(arguments[i]),'Non-finite sprite coordinate');};
  nativeSend=function(buffer){sent.push(Array.from(new Uint8Array(buffer)));};
  function varint(n){const out=[];do{const b=n%128;n=Math.floor(n/128);out.push(b|(n?128:0));}while(n);return out;}
  function str(s){return varint(s.length).concat(Array.from(s,c=>c.charCodeAt(0)));}
  function packet(bytes){return new Uint8Array(bytes).buffer;}
  Snow.open(42);
  Snow.create(1,2,3,3,1,2);
  const create=sent[sent.length-1], contract=String.fromCharCode.apply(null,create.slice(9));
  check(contract.length===64,'CREATE contract missing');
  Snow.join(7);check(sent[sent.length-1].slice(3).join()===create.slice(8).join(),'JOIN contract differs from CREATE');
  const row=[...varint(7),...varint(42),...str('Host'),1,2,3,3,1,2,0,6,0,1,42,...str('Host'),1];
  Snow.receive(packet([1,1,...row,1,7,...str('obsolete')]),42,0);
  check(JSON.parse(Snow.status(0)).games.length===0,'Incompatible game exposed');
  Snow.receive(packet([1,1,...row,1,7,...str(contract)]),42,0);
  check(JSON.parse(Snow.status(0)).games.length===1,'Compatible game hidden');
  let refused=false;try{Snow.receive(packet([5,1]),42,0);}catch(e){refused=String(e).includes('versions differ');}check(refused,'Contract rejection not handled');
  for(const arena of [1,2,3,7]){
    Snow.create(arena,2,3,2,1,2);Snow.practice(42,0,'Tester',0);
    for(let now=0;now<=6000;now+=60){Snow.tick(now);Snow.draw(now);}
    check(JSON.parse(Snow.status(6000)).state!==null,'Practice failed');Snow.leave();
  }
  check(sprites>1000,'No native scene drawn');
  print('Native Snow Storm bridge: contracts and four arenas rendered; '+sprites+' sprites.');
})();
