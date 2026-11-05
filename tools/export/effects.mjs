// Resolve effect layers with the actual web controller before native packing.
import {cp,mkdir,mkdtemp,readFile,writeFile,rm} from 'node:fs/promises';
import {join,resolve,sep} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
import {execFileSync} from 'node:child_process';
import {chromium} from 'playwright-core';

const here=import.meta.dirname,web=resolve(process.env.TWEEBBO_WEB??join(here,'../../../hotel'));
const output=resolve(process.argv[2]??join(here,'../../build/effects-export'));
const work=await mkdtemp(join(tmpdir(),'tweebbo-effects-')),snapshot=join(work,'web');
let browser,server;
try{
  await mkdir(snapshot,{recursive:true});
  await cp(join(web,'client/src'),join(snapshot,'src'),{recursive:true});
  await cp(join(web,'client/public'),join(snapshot,'public'),{recursive:true});
  await cp(join(web,'client/tsconfig.json'),join(snapshot,'tsconfig.json'));
  execFileSync(join(here,'node_modules/.bin/vite'),['build','--config',join(here,'vite.config.mjs')],{
    stdio:'pipe',env:{...process.env,TWEEBBO_SNAPSHOT:snapshot,TWEEBBO_EXPORT_BUILD:join(work,'build')},
  });
  server=createServer(async(req,res)=>{
    try{
      const path=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
      if(path==='/'){res.setHeader('Content-Type','text/html');return res.end('<!doctype html><html lang="en"><title>Effect export</title><script type="module" src="/export.js"></script></html>');}
      const file=path==='/export.js'?join(work,'build/export.js'):path.startsWith('/static/')?resolve(snapshot,'public',path.slice(8)):'';
      if(!file||!file.startsWith(work+sep))throw Error('Unknown export resource');
      res.setHeader('Content-Type',file.endsWith('.js')?'text/javascript':file.endsWith('.json')?'application/json':file.endsWith('.png')?'image/png':'application/octet-stream');
      res.end(await readFile(file));
    }catch{res.writeHead(404);res.end();}
  });
  await new Promise(done=>server.listen(0,'127.0.0.1',done));
  browser=await chromium.launch({executablePath:process.env.CHROME_PATH??'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>typeof window.exportEffects==='function');
  const data=await page.evaluate(()=>window.exportEffects());
  if(errors.length)throw Error(errors.join('\n'));
  await mkdir(join(output,'images'),{recursive:true});await mkdir(join(output,'previews'),{recursive:true});
  for(const image of data.images){await writeFile(join(output,'images',`${image.id}.png`),Buffer.from(image.png,'base64'));delete image.png;}
  for(const image of data.previews){await writeFile(join(output,'previews',`fx-${image.id}-${image.dir}${image.pose?'-'+image.pose:''}.png`),Buffer.from(image.png,'base64'));delete image.png;}
  const sources={};for(const name of ['src/avatar.ts','src/avatar-effects.ts','public/models/avatar-effects.json','public/models/avatar-effects-0.png'])sources[name]=createHash('sha256').update(await readFile(join(snapshot,name))).digest('hex');
  data.source={web,exportedAt:new Date().toISOString(),sources};
  await writeFile(join(output,'effects.json'),JSON.stringify(data)+'\n');
  console.log(JSON.stringify({effects:data.animations.length,frames:data.frames.length,images:data.images.length,previews:data.previews.length,output}));
}finally{
  if(browser)await browser.close();if(server)await new Promise(done=>server.close(done));
  await rm(work,{recursive:true,force:true});
}
