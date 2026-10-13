// Export page: uses the same drawing code as the web client
// (a snapshot of the web client sources) to get identical sprites.
import { loadModels, people, poolPeople, spriteImage } from '@web/models';
import { catalogue, ensureFurniture } from '@web/catalogue';
import { furniLayers, furniFrame, FURNI_OX, FURNI_OY } from '@web/furni';
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
  return out;
};

// Every definition with graphics: the console can enter any room.
window.furniIds = async () => {
  await loadModels();
  return Object.values(catalogue.definitions).filter((d) => d.asset).map((d) => d.id).sort((a, b) => a - b);
};

// Maximum exported states (100-state scoreboards repeat digits).
const MAX_STATES = 12;

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
      const states = def.kind === 4 || id === 7 ? 8 : def.flags.includes('teleporter') ? 3 : Math.min(MAX_STATES, Math.max(1, def.states));
      for (let state = 0; state < states; state++) {
       // Frames of animated furniture (furniFrame): holograms, teleporting, dice, lamp.
       const frames = new Set<number>();
       for (let t = 0; t < 6400; t += 10) frames.add(furniFrame(id, state, t));
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
