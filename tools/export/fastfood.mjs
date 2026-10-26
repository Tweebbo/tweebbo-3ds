import path from 'node:path';
import {mkdir,readFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {build} from 'esbuild';
const root=path.resolve(import.meta.dirname,'../..');
const web=process.env.TWEEBBO_WEB||path.resolve(root,'../Tweebbo-games-candidate-20261025');
const require=createRequire(path.join(web,'client/package.json')),ts=require('typescript');
const source=await readFile(path.join(web,'client/src/avatar.ts'),'utf8'),ast=ts.createSourceFile('avatar.ts',source,ts.ScriptTarget.Latest,true);
const avatarFunctions=ast.statements.filter(s=>ts.isFunctionDeclaration(s)&&['decodeLook','lookGender'].includes(s.name?.text)).map(s=>s.getText(ast)).join('\n');
await mkdir(path.join(root,'romfs/fastfood'),{recursive:true});
await build({entryPoints:[path.join(root,'tools/fastfood/bridge.ts')],bundle:true,format:'iife',target:'es2020',outfile:path.join(root,'romfs/fastfood/game.js'),plugins:[{name:'original-fastfood',setup(b){
 b.onResolve({filter:/^fastfood-original\//},a=>({path:path.join(web,'client/src/fastfood',a.path.slice(18)+'.ts')}));
 b.onResolve({filter:/^fastfood-lookup$/},()=>({path:path.join(root,'build/fastfood/lookup.json')}));
 b.onResolve({filter:/^\.\.\/(avatar|models|i18n)$/},a=>({path:a.path,namespace:'native-shim'}));
 b.onLoad({filter:/.*/,namespace:'native-shim'},a=>({contents:a.path.endsWith('avatar')?`import {Head} from ${JSON.stringify(path.join(root,'tools/fastfood/canvas.ts'))}; export const avatarSprite=(look)=>new Head(look);import wardrobe from ${JSON.stringify(path.join(web,'client/src/wardrobe-data.json'))};${avatarFunctions}`:a.path.endsWith('models')?'export const people=true;export async function loadModels(){}':"export const CANVAS_FONT='sans-serif';export const t=(s,args={})=>s.replace(/\\{(\\w+)\\}/g,(_,k)=>String(args[k]??k));",loader:'ts',resolveDir:root}));
}}]});
console.log('Original Fast Food engine and renderer bundled for native QuickJS.');
