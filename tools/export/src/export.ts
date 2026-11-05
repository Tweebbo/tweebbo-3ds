// Export page: uses the same drawing code as the web client
// (a snapshot of the web client sources) to get identical sprites.
import { loadModels, people, poolPeople, spriteImage, loadBarItems, barItems } from '@web/models';
import { drawHeld } from '@web/after-hours-art';
import { catalogue, ensureFurniture, furniAnim } from '@web/catalogue';
import { furniLayers, furniFrame, importedSmall, animationPeriod, layerFrame, layerOffset, FURNI_OX, FURNI_OY } from '@web/furni';
import { Renderer } from '@web/renderer';
import { State } from '@web/state';
import { loadClassic } from '@web/classic';
import {loadAvatarEffects,avatarEffectBody,avatarEffectLayers,avatarEffectShadow,avatarEffectSprite} from '@web/avatar-effects';
import {avatarSprite,ANCHOR_X,ANCHOR_Y,SWIM_ANCHOR_X} from '@web/avatar';

// Parts the avatar tints (same set as avatar.ts).
const TINT = new Set(['bd', 'hd', 'fc', 'lh', 'rh', 'hr', 'ch', 'ls', 'rs', 'lg', 'sh']);
/** Crops a canvas to its visible content. */
function crop(c: HTMLCanvasElement | null): { c: HTMLCanvasElement; x: number; y: number } | null {
  if (!c) return null;
  const d = c.getContext('2d')!.getImageData(0, 0, c.width, c.height).data;
  let x0 = c.width, y0 = c.height, x1 = -1, y1 = -1;
  for (let y = 0; y < c.height; y++) for (let x = 0; x < c.width; x++) if (d[(y * c.width + x) * 4 + 3]) {
    if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
  }
  if (x1 < 0) return null;
  const out = document.createElement('canvas');
  out.width = x1 - x0 + 1; out.height = y1 - y0 + 1;
  out.getContext('2d')!.drawImage(c, -x0, -y0);
  return { c: out, x: x0, y: y0 };
}
const png = (c: HTMLCanvasElement) => c.toDataURL('image/png').slice('data:image/png;base64,'.length);

declare global {
  interface Window {
    exportPeople(prefix: string): Promise<unknown[]>;
    exportFurni(defs: number[],rotation?:number,state?:number): Promise<unknown[]>;
    furniturePlan():Promise<{id:number;rotations:number[];states:number}[]>;
    exportIcons(defs: number[]): Promise<{ def: number; png: string }[]>;
    furnitureTimelines(): Record<string, number[]>;
    takeFurnitureImages(): {id:number;png:string}[];
    furniIds(): Promise<number[]>;
    exportFloors(models: { key: string; rows: string[]; door: [number, number] }[]): Promise<unknown[]>;
    exportSwimming(): Promise<unknown[]>;
    exportEffects(): Promise<Record<string, unknown>>;
  }
}

window.exportEffects = async () => {
  await loadModels();await loadAvatarEffects();
  const source=await (await fetch('/static/models/avatar-effects.json')).json();
  const images:{id:number;w:number;h:number;png:string}[]=[],frames:unknown[]=[],animations:unknown[]=[],previews:unknown[]=[];
  const imageObjects=new WeakMap<HTMLCanvasElement,number>(),imageContent=new Map<string,number>(),frameContent=new Map<string,number>();
  const picture=(image:HTMLCanvasElement)=>{
    const known=imageObjects.get(image);if(known!==undefined)return known;
    const data=png(image);let id=imageContent.get(data);
    if(id===undefined){id=images.length;images.push({id,w:image.width,h:image.height,png:data});imageContent.set(data,id);}
    imageObjects.set(image,id);return id;
  };
  const layer=(part:ReturnType<typeof avatarEffectShadow>)=>part?{image:picture(part.image),x:part.x,y:part.y,ink:part.ink,depth:part.depth,alpha:part.alpha??1}:null;
  const frame=(id:number,dir:number,tick:number,moving:boolean)=>{
    const body=avatarEffectBody(id,dir,tick);
    const row={dx:body.dx,dy:body.dy,hide:body.body?.hide??[],parts:body.body?.parts??{},layers:avatarEffectLayers(id,dir,tick,moving).map(layer)};
    const key=JSON.stringify(row);let index=frameContent.get(key);
    if(index===undefined){index=frames.length;frames.push(row);frameContent.set(key,index);}return index;
  };
  const gcd=(a:number,b:number):number=>b?gcd(b,a%b):a;
  for(const [key,animation] of Object.entries(source.animations).sort((a,b)=>Number(a[0].slice(3))-Number(b[0].slice(3)))){
    const id=Number(key.slice(3));if(!Number.isInteger(id)||id<=0)continue;
    const raw=animation as {name:string;rules?:{frames:unknown[];override?:unknown};fx?:unknown[];frames:unknown[];avatar?:unknown};
    const period=Math.max(1,raw.rules?.frames.length??0,raw.fx?.length??0,raw.frames.length);
    const movingPeriod=raw.rules?.override?period*4/gcd(period,4):period;
    const directions=Array.from({length:8},(_,dir)=>({shadow:layer(avatarEffectShadow(id,dir)),
      still:Array.from({length:period},(_,tick)=>frame(id,dir,tick,false)),moving:Array.from({length:movingPeriod},(_,tick)=>frame(id,dir,tick,true))}));
    animations.push({id,name:raw.name,period,movingPeriod,appearance:raw.avatar??null,directions});
    if([2,3,11,12,13,38,45,46,55].includes(id))for(const dir of [2,5]){
      const tick=3,moving=id===38||id===55,body=avatarEffectBody(id,dir,tick);
      const canvas=document.createElement('canvas');canvas.width=192;canvas.height=192;const ctx=canvas.getContext('2d')!;
      ctx.fillStyle='#0c1115';ctx.fillRect(0,0,192,192);
      const foot={x:96,y:160},parts=avatarEffectLayers(id,dir,tick,moving),shadow=avatarEffectShadow(id,dir);
      const draw=(part:ReturnType<typeof avatarEffectShadow>)=>{if(!part)return;ctx.save();ctx.globalAlpha=part.alpha??1;ctx.globalCompositeOperation=part.ink===33?'lighter':'source-over';ctx.drawImage(part.image,foot.x-part.x,foot.y-part.y);ctx.restore();};
      draw(shadow?{...shadow,alpha:.25}:undefined);parts.filter(p=>p.depth<0).forEach(draw);
      const avatar=avatarSprite(0,dir,moving?'walk':'std',tick,0,dir,false,0,false,false,false,false,0,1,false,'',body.body);
      ctx.drawImage(avatarEffectSprite(avatar,id),foot.x-ANCHOR_X+body.dx,foot.y-ANCHOR_Y+body.dy);
      parts.filter(p=>p.depth>=0).forEach(draw);previews.push({id,dir,tick,moving,png:png(canvas)});
    }
    if([2,11,12,13].includes(id))for(const pose of ['float','swim'] as const)for(const dir of [2,5]){
      const tick=3,moving=pose==='swim',canvas=document.createElement('canvas');canvas.width=192;canvas.height=192;
      const ctx=canvas.getContext('2d')!;ctx.fillStyle='#0c1115';ctx.fillRect(0,0,192,192);
      const parts=avatarEffectLayers(id,dir,tick,moving);
      const draw=(part:ReturnType<typeof avatarEffectShadow>)=>{if(!part)return;ctx.save();ctx.globalAlpha=part.alpha??1;ctx.globalCompositeOperation=part.ink===33?'lighter':'source-over';ctx.drawImage(part.image,96-part.x,160-part.y);ctx.restore();};
      parts.filter(p=>p.depth<0).forEach(draw);
      const avatar=avatarSprite(0,dir,pose,tick,0,dir);
      ctx.drawImage(avatarEffectSprite(avatar,id),96-SWIM_ANCHOR_X,160-ANCHOR_Y);
      parts.filter(p=>p.depth>=0).forEach(draw);previews.push({id,dir,tick,moving,pose,png:png(canvas)});
    }
  }
  return {version:1,tickMilliseconds:80,animations,frames,images,previews};
};

window.exportPeople = async (prefix) => {
  await loadModels();
  const out = [];
  for (const [name, s] of Object.entries(people.sprites)) {
    if (!name.startsWith(prefix + '_')) continue;
    const part = name.split('_')[2];
    // Tintable parts are exported white with interior holes
    // filled; the 3DS later multiplies them by the avatar color.
    const tint = TINT.has(part);
    const img = spriteImage(people, name, tint ? '#ffffff' : undefined);
    if (!img) continue;
    out.push({ name, w: s.w, h: s.h, reg: s.reg, tint, png: png(img) });
  }
  if (prefix === 'h') {
    // After Hours: the original bar drinks (client/public/bar) as ordinary hand item models,
    // and the drawn whisky/cigarette/cigar (200-202, after-hours-art.ts) pre-rendered over the
    // cup's registration for every direction and pose (the console draws them like any drink).
    await loadBarItems();
    for (const [name, s] of Object.entries(barItems!.sprites)) {
      if (people.sprites[name]) continue;
      const img = spriteImage(barItems!, name);
      if (img) out.push({ name, w: s.w, h: s.h, reg: s.reg, tint: false, png: png(img) });
    }
    for (const item of [200, 201, 202]) for (const action of ['crr', 'drk']) for (let d = 0; d < 8; d++) {
      const baseName = `h_${action}_ri_001_${d}_0`, s = people.sprites[baseName];
      const base = s && spriteImage(people, baseName);
      if (!s || !base) continue;
      const pad = 16, c = document.createElement('canvas');
      c.width = s.w + pad * 2; c.height = s.h + pad * 2;
      drawHeld(c.getContext('2d')!, item, d, action === 'drk', pad, pad, 0, base);
      const cut = crop(c);
      if (!cut) continue;
      out.push({ name: `h_${action}_ri_${item}_${d}_0`, w: cut.c.width, h: cut.c.height,
        reg: [s.reg[0] + pad - cut.x, s.reg[1] + pad - cut.y], tint: false, png: png(cut.c) });
    }
  }
  return out;
};

// Every definition with graphics: the console can enter any room.
window.furniIds = async () => {
  await loadModels();
  return Object.values(catalogue.definitions).filter((d) => d.asset).map((d) => d.id).sort((a, b) => a - b);
};


// Catalogue and inventory icons as the web draws them (importedSmall): one per definition, with
// the definition's own colours. The bank's -icon.png is shared by every colour and only holds the
// uncoloured layer, so coloured furniture showed as white outlines.
window.exportIcons = async (defs) => {
  await loadModels();
  const out = [];
  for (const id of defs) {
    if (!catalogue.definitions[id]?.asset) continue;
    await ensureFurniture(id);
    out.push({ def: id, png: png(importedSmall(id)) });
  }
  return out;
};

const timelines: Record<string, number[]> = {};
const furnitureImages=new WeakMap<HTMLCanvasElement,number>();
let nextFurnitureImage=0;
const pendingFurnitureImages:{id:number;png:string}[]=[];
const furnitureImage=(canvas:HTMLCanvasElement):number=>{
  let id=furnitureImages.get(canvas);if(id!==undefined)return id;
  id=nextFurnitureImage++;furnitureImages.set(canvas,id);pendingFurnitureImages.push({id,png:png(canvas)});return id;
};
window.takeFurnitureImages=()=>pendingFurnitureImages.splice(0);
window.furnitureTimelines = () => timelines;
window.furniturePlan=async()=>{await loadModels();return Object.values(catalogue.definitions).filter(d=>d.asset).map(d=>({id:d.id,rotations:d.rotations?.length?d.rotations:[0,1,2,3],states:d.sprite==='bottle'?9:d.kind===4||d.id===7?8:d.flags.includes('teleporter')?3:Math.max(1,d.states)}));};
window.exportFurni = async (defs,onlyRotation,onlyState) => {
  await loadModels();
  const out = [];
  for (const id of defs) {
    const def = catalogue.definitions[id];
    if (!def?.asset) continue;
    await ensureFurniture(id);
    for (const rot of def.rotations?.length ? def.rotations : [0, 1, 2, 3]) {
      if(onlyRotation!==undefined&&rot!==onlyRotation)continue;
      // States the server uses even if the catalogue does not count them: dice (face 1–6, 7 = rolling)
      // and teleporters (1 open, 2 travelling).
      const states = def.sprite==='bottle'?9:def.kind === 4 || id === 7 ? 8 : def.flags.includes('teleporter') ? 3 : Math.max(1, def.states);
      for (let state = 0; state < states; state++) {
       if(onlyState!==undefined&&state!==onlyState)continue;
       // Frames of animated furniture (furniFrame): holograms, teleporting, dice, lamp.
       const frames = new Set<number>([0]);
       const first = furniFrame(id, state, 0);
       if (first >= 1000) {
         const sequence = furniAnim[def.asset.bank]?.states[String(state)];
         const period = animationPeriod(sequence ?? {});
         const aliases: number[] = [], unique = new Map<string, number>();
         for (let tick = 0; tick < period; tick++) {
           const signature = Object.entries(sequence ?? {}).map(([layer, seq]) => {
             return `${layer}:${layerFrame(seq,tick,layer)}:${layerOffset(seq,tick,layer)}`;
           }).join('/');
           let canonical = unique.get(signature);
           if (canonical === undefined) { canonical = 1000 + tick; unique.set(signature, canonical); frames.add(canonical); }
           aliases.push(canonical);
         }
         timelines[`${id}/${state}`] = aliases;
       } else for (let t = 0; t < 6400; t += 10) frames.add(furniFrame(id, state, t));
       for (const frame of [...frames].sort((a, b) => a - b)) {
        const layers = furniLayers(id, rot, state, frame);
        let parts = (layers.parts ?? []).map((p) => ({
          x: p.x, y: p.y, depth: p.depth, alpha: p.alpha, shadow: p.shadow,
          add: p.composite === 'lighter', w: p.image.width, h: p.image.height, image: furnitureImage(p.image),
        }));
        // Furniture 1–8 from the first edition only has 512×512 base and front layers.
        if (!layers.parts) parts = [[layers.base, -0.006], [layers.front, 0.133]].flatMap(([c, depth]) => {
          const cut = crop(c as HTMLCanvasElement | null);
          return cut ? [{ x: cut.x - FURNI_OX, y: cut.y - FURNI_OY, depth: depth as number, alpha: 1, shadow: false, add: false, w: cut.c.width, h: cut.c.height, image: furnitureImage(cut.c) }] : [];
        });
        out.push({ def: id, rot, state, frame, parts });
       }
      }
    }
  }
  return out;
};

// Floor and walls of rooms without an original background:
// the same Renderer.buildFloor() the web uses, unmodified.
window.exportFloors = async (models) => {
  await loadModels();
  await loadClassic();
  const out = [];
  for (const m of models) {
    const st = new State();
    st.model = m.key;
    st.h = m.rows.length;
    st.w = m.rows[0].length;
    st.tiles = [];
    for (let y = 0; y < st.h; y++) for (let x = 0; x < st.w; x++) st.tiles.push(m.rows[y][x] !== 'x');
    st.door = m.door;
    const canvas = document.createElement('canvas');
    canvas.style.width = '400px'; canvas.style.height = '240px';
    document.body.append(canvas);
    const r = new Renderer(canvas, st) as unknown as { buildFloor(): void; floor: HTMLCanvasElement; fox: number; foy: number };
    r.buildFloor();
    out.push({ key: m.key, x: r.fox, y: r.foy, w: r.floor.width, h: r.floor.height, png: png(r.floor) });
    canvas.remove();
  }
  return out;
};

// Swimming bodies (swimming atlas) in white: the console tints them.
window.exportSwimming = async () => {
  await loadModels();
  const pool = poolPeople!;
  const out = [];
  for (const [name, s] of Object.entries(pool.sprites)) {
    const img = spriteImage(pool, name, '#ffffff', true);
    if (img) out.push({ name, w: s.w, h: s.h, reg: s.reg, png: png(img) });
  }
  return out;
};

// Full avatars for the HOME Menu icon and banner (same compositor as the web).
import { avatarSprite, encodeLook } from '@web/avatar';
declare global { interface Window { exportAvatars(list: { look: Record<string, number>; dir: number; pose: string; frame: number; drink?: number }[]): Promise<string[]>; } }
window.exportAvatars = async (list) => {
  await loadModels();
  return list.map((a) => png(avatarSprite(encodeLook(a.look as never), a.dir, a.pose as never, a.frame, 0, a.dir, false, a.drink ?? 0, false)));
};
