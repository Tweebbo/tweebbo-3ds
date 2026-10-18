// Export page: uses the same drawing code as the web client
// (a snapshot of the web client sources) to get identical sprites.
import { loadModels, people, poolPeople, spriteImage, loadBarItems, barItems } from '@web/models';
import { drawHeld } from '@web/after-hours-art';
import { catalogue, ensureFurniture, furniAnim } from '@web/catalogue';
import { furniLayers, furniFrame, importedSmall, FURNI_OX, FURNI_OY } from '@web/furni';
import { Renderer } from '@web/renderer';
import { State } from '@web/state';
import { loadClassic } from '@web/classic';

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
    exportFurni(defs: number[]): Promise<unknown[]>;
    exportIcons(defs: number[]): Promise<{ def: number; png: string }[]>;
    furnitureTimelines(): Record<string, number[]>;
    furniIds(): Promise<number[]>;
    exportFloors(models: { key: string; rows: string[]; door: [number, number] }[]): Promise<unknown[]>;
    exportSwimming(): Promise<unknown[]>;
  }
}

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

const gcd = (a: number, b: number): number => b ? gcd(b, a % b) : a;

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
window.furnitureTimelines = () => timelines;
window.exportFurni = async (defs) => {
  await loadModels();
  const out = [];
  for (const id of defs) {
    const def = catalogue.definitions[id];
    if (!def?.asset) continue;
    await ensureFurniture(id);
    for (const rot of def.rotations?.length ? def.rotations : [0, 1, 2, 3]) {
      // States the server uses even if the catalogue does not count them: dice (face 1–6, 7 = rolling)
      // and teleporters (1 open, 2 travelling).
      const states = def.kind === 4 || id === 7 ? 8 : def.flags.includes('teleporter') ? 3 : Math.max(1, def.states);
      for (let state = 0; state < states; state++) {
       // Frames of animated furniture (furniFrame): holograms, teleporting, dice, lamp.
       const frames = new Set<number>([0]);
       const first = furniFrame(id, state, 0);
       if (first >= 1000) {
         const sequence = furniAnim[def.asset.bank]?.states[String(state)];
         let period = 1;
         for (const layer of Object.values(sequence ?? {})) {
           const n = layer.d * layer.f.length;
           period = Math.min(4096, period / gcd(period, n) * n);
         }
         const aliases: number[] = [], unique = new Map<string, number>();
         for (let tick = 0; tick < period; tick++) {
           const signature = Object.entries(sequence ?? {}).map(([layer, seq]) => {
             const step = Math.floor(tick / seq.d);
             let index = step % seq.f.length;
             if (seq.r) {
               let h = (step * 2654435761 + layer.charCodeAt(0) * 40503) >>> 0;
               h = (h ^ (h >>> 15)) >>> 0; h = Math.imul(h, 2246822519) >>> 0; h = (h ^ (h >>> 13)) >>> 0;
               index = h % seq.f.length;
             }
             return `${layer}:${seq.f[index]}`;
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
          add: p.composite === 'lighter', w: p.image.width, h: p.image.height, png: png(p.image),
        }));
        // Furniture 1–8 from the first edition only has 512×512 base and front layers.
        if (!layers.parts) parts = [[layers.base, -0.006], [layers.front, 0.133]].flatMap(([c, depth]) => {
          const cut = crop(c as HTMLCanvasElement | null);
          return cut ? [{ x: cut.x - FURNI_OX, y: cut.y - FURNI_OY, depth: depth as number, alpha: 1, shadow: false, add: false, w: cut.c.width, h: cut.c.height, png: png(cut.c) }] : [];
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
