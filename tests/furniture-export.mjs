import assert from 'node:assert/strict';
import {readFile,stat,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';

const root=resolve(import.meta.dirname,'..'),source=resolve(process.argv[2]??root+'/build/export');
const variants=JSON.parse(await readFile(source+'/furni.json','utf8'));
const timelines=JSON.parse(await readFile(source+'/furni-timelines.json','utf8'));
const groups=new Map(),images=new Map();
for(const variant of variants){
  const key=`${variant.def}/${variant.state}/${variant.rot}`;
  if(!groups.has(key))groups.set(key,new Map());
  groups.get(key).set(variant.frame,variant);
}
let checked=0,moving=0;
for(const [key,frames] of Object.entries(timelines)){
  assert(frames.length>0&&frames.length<=65535,key);
  const rotations=[...groups].filter(([group])=>group.startsWith(key+'/'));assert(rotations.length,key);
  for(const [group,variants] of rotations){
    const signatures=new Set();
    for(const frame of new Set(frames)){
      const variant=variants.get(frame);assert(variant,`${group}: missing frame ${frame}`);
      const parts=[];
      for(const part of variant.parts){
        if(!images.has(part.file)){
          const file=source+'/furni/'+part.file;assert((await stat(file)).size>0,file);
          images.set(part.file,createHash('sha256').update(await readFile(file)).digest('hex'));
        }
        parts.push([images.get(part.file),part.x,part.y,part.depth,part.alpha,part.add]);
      }
      signatures.add(JSON.stringify(parts));
    }
    checked++;if(signatures.size>1)moving++;
  }
}
assert(moving>0);
const report={timelines:Object.keys(timelines).length,rotationStates:checked,movingRotationStates:moving,referencedImages:images.size,missingFrames:0};
await writeFile(root+'/build/furniture-export-checks.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
