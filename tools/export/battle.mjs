import path from 'node:path';
import {mkdir} from 'node:fs/promises';
import {build} from 'esbuild';
const root=path.resolve(import.meta.dirname,'../..');
const web=process.env.TWEEBBO_WEB||path.resolve(root,'../Tweebbo-games-candidate-20261025');
await mkdir(path.join(root,'romfs/battle'),{recursive:true});
await build({entryPoints:[path.join(root,'tools/battle/bridge.ts')],bundle:true,format:'iife',target:'es2020',outfile:path.join(root,'romfs/battle/game.js'),plugins:[{name:'original-battle',setup(b){
 b.onResolve({filter:/^battle-original\//},a=>({path:path.join(web,'client/src',a.path.slice(16)+'.ts')}));
 b.onResolve({filter:/^battle-lookup$/},()=>({path:path.join(root,'build/battle/lookup.json')}));
}}]});
console.log('Battle Ball native renderer bundled with original cameras, animation sequences and tile reveal.');
