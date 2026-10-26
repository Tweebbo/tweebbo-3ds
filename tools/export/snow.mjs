import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { build } from 'esbuild';
const root = path.resolve(import.meta.dirname, '../..');
const web = process.env.TWEEBBO_WEB || path.resolve(root, '../Tweebbo-Vercel');
const require = createRequire(path.join(web, 'client/package.json'));
const ts = require('typescript');
const avatar = fs.readFileSync(path.join(web, 'client/src/avatar.ts'), 'utf8');
const ast = ts.createSourceFile('avatar.ts', avatar, ts.ScriptTarget.Latest, true);
const wanted = new Set(['SKIN', 'HAIR', 'SHIRT', 'decodeLook']);
const shim = ["import wardrobe from 'wardrobe'; export { wardrobe };"];
for (const s of ast.statements) {
  if (ts.isFunctionDeclaration(s) && wanted.has(s.name?.text) || ts.isVariableStatement(s) && s.declarationList.declarations.some(d => wanted.has(d.name.getText(ast)))) shim.push(s.getText(ast));
}
await build({
  entryPoints: [path.join(root, 'tools/snow/bridge.ts')], bundle: true, format: 'iife', target: 'es2020',
  outfile: path.join(root, 'romfs/snow/game.js'),
  plugins: [{ name: 'original-snowwar', setup(b) {
    b.onResolve({ filter: /^snow-original\// }, a => ({ path: path.join(web, 'client/src/snowstorm', a.path.slice(14) + '.ts') }));
    b.onResolve({ filter: /^snow-data$/ }, () => ({ path: path.join(web, 'client/public/snowstorm/game.json') }));
    b.onResolve({ filter: /^snow-sheet$/ }, () => ({ path: path.join(web, 'client/public/snowstorm/sprites.json') }));
    b.onResolve({ filter: /^snow-lookup$/ }, () => ({ path: path.join(root, 'build/snow/lookup.json') }));
    b.onResolve({ filter: /^\.\.\/avatar$/ }, () => ({ path: 'avatar', namespace: 'native-avatar' }));
    b.onLoad({ filter: /.*/, namespace: 'native-avatar' }, () => ({ contents: shim.join('\n'), loader: 'ts' }));
    b.onResolve({ filter: /^wardrobe$/ }, () => ({ path: path.join(web, 'client/src/wardrobe-data.json') }));
    // Native menus translate through the bounded C dictionary, outside the simulation VM.
    b.onResolve({ filter: /^\.\.\/i18n$/ }, () => ({ path: 'i18n', namespace: 'native-language' }));
    b.onLoad({ filter: /.*/, namespace: 'native-language' }, () => ({ contents: `
      export const CANVAS_FONT = 'sans-serif';
      export const t = (text,vars) => {
        const translated = globalThis.nativeTranslate ? globalThis.nativeTranslate(text) : text;
        return vars ? translated.replace(/\\{(\\w+)\\}/g,(all,key)=>Object.prototype.hasOwnProperty.call(vars,key)?String(vars[key]):all) : translated;
      };` }));
  }}],
});
console.log('Snow Storm engine, renderer, wardrobe and native adapter bundled.');
