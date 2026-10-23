// Exports sprites from the web client to PNG + JSON for the 3DS packer.
// Usage: node run.mjs <output> [comma-separated furniture definitions | all]
// Reads TWEEBBO_WEB (the web edition checkout) without modifying it.
import { cp, mkdir, rm, writeFile, readFile, readdir } from 'node:fs/promises';
import { createServer } from 'node:http';
import { join, resolve, extname } from 'node:path';
import { homedir, tmpdir } from 'node:os';
import { execFileSync } from 'node:child_process';
import { chromium } from 'playwright-core';

const here = import.meta.dirname;
const outDir = resolve(process.argv[2] ?? join(here, '../../build/export'));
// Without a list (or with "all"), every definition with graphics is exported.
const defArg = process.argv[3] ?? 'all';
let defs = defArg === 'all' ? null : defArg.split(',').filter(Boolean).map(Number);
const web = resolve(process.env.TWEEBBO_WEB ?? join(homedir(), 'Tweebbo-Vercel'));
const work = join(process.env.TMPDIR ?? tmpdir(), `tweebbo-3ds-export-${process.pid}`);

// Snapshot: the web client may be changing while we export.
const snapshot = join(work, 'web');
await mkdir(snapshot, { recursive: true });
await cp(join(web, 'client/src'), join(snapshot, 'src'), { recursive: true });
await cp(join(web, 'client/public'), join(snapshot, 'public'), { recursive: true });
await cp(join(web, 'client/tsconfig.json'), join(snapshot, 'tsconfig.json'));

execFileSync(join(here, 'node_modules/.bin/vite'), ['build', '--config', join(here, 'vite.config.mjs')], {
  stdio: 'inherit', env: { ...process.env, TWEEBBO_SNAPSHOT: snapshot, TWEEBBO_EXPORT_BUILD: join(work, 'build') },
});

const types = { '.js': 'text/javascript', '.json': 'application/json', '.png': 'image/png', '.html': 'text/html' };
const server = createServer(async (req, res) => {
  const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  try {
    let file;
    if (path === '/') { res.setHeader('content-type', 'text/html'); return res.end('<!doctype html><script type="module" src="/export.js"></script>'); }
    if (path === '/export.js') file = join(work, 'build/export.js');
    else if (path.startsWith('/static/')) file = join(snapshot, 'public', path.slice(8));
    if (!file || !file.startsWith(work)) throw new Error('no');
    res.setHeader('content-type', types[extname(file)] ?? 'application/octet-stream');
    res.end(await readFile(file));
  } catch { res.statusCode = 404; res.end(); }
});
await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
const url = `http://127.0.0.1:${server.address().port}/`;

const browser = await chromium.launch({ executablePath: process.env.CHROME ?? '/usr/bin/google-chrome', headless: true });
try {
  const page = await browser.newPage();
  page.on('pageerror', (e) => console.error('page:', e.message));
  await page.goto(url);
  await page.waitForFunction(() => typeof window.exportPeople === 'function');

  await rm(outDir, { recursive: true, force: true });
  await mkdir(join(outDir, 'people'), { recursive: true });
  await mkdir(join(outDir, 'furni'), { recursive: true });

  const people = await page.evaluate(() => window.exportPeople('h'));
  const peopleMeta = [];
  for (const s of people) {
    await writeFile(join(outDir, 'people', `${s.name}.png`), Buffer.from(s.png, 'base64'));
    const { png, ...meta } = s; peopleMeta.push(meta);
  }
  await writeFile(join(outDir, 'people.json'), JSON.stringify(peopleMeta));
  console.log(`personas: ${peopleMeta.length} sprites`);

  // Rooms without an original background: geometry from server/src/room.rs (MODELS).
  const roomRs = await readFile(join(web, 'server/src/room.rs'), 'utf8');
  const procedural = [];
  for (const m of roomRs.matchAll(/key:\s*"((?!model_)[a-z]+)",[\s\S]*?rows:\s*&\[([\s\S]*?)\],[\s\S]*?door:\s*\((\d+),\s*(\d+)\)/g))
    procedural.push({ key: m[1], rows: [...m[2].matchAll(/"([x0-9]+)"/g)].map((r) => r[1]), door: [Number(m[3]), Number(m[4])] });
  await mkdir(join(outDir, 'floors'), { recursive: true });
  const floors = await page.evaluate((m) => window.exportFloors(m), procedural);
  const floorMeta = [];
  for (const f of floors) {
    await writeFile(join(outDir, 'floors', `${f.key}.png`), Buffer.from(f.png, 'base64'));
    const { png, ...meta } = f; floorMeta.push({ ...meta, rows: procedural.find((p) => p.key === f.key).rows, door: procedural.find((p) => p.key === f.key).door });
  }
  await writeFile(join(outDir, 'floors.json'), JSON.stringify(floorMeta));
  console.log(`floors: ${floorMeta.map((f) => f.key).join(', ')}`);

  await mkdir(join(outDir, 'swimming'), { recursive: true });
  const swim = await page.evaluate(() => window.exportSwimming());
  const swimMeta = [];
  for (const s of swim) {
    await writeFile(join(outDir, 'swimming', `${s.name}.png`), Buffer.from(s.png, 'base64'));
    const { png, ...meta } = s; swimMeta.push(meta);
  }
  await writeFile(join(outDir, 'swimming.json'), JSON.stringify(swimMeta));
  console.log(`swimming: ${swimMeta.length} sprites`);

  if (!defs) defs = await page.evaluate(() => window.furniIds());
  const furniMeta = [];
  const plan=new Map((await page.evaluate(()=>window.furniturePlan())).map(d=>[d.id,d]));
  let n = 0;
  // Export each definition separately; particle timelines can contain thousands of variants.
  for (let i = 0; i < defs.length; i++) {
    for(const rotation of plan.get(defs[i]).rotations)for(let state=0;state<plan.get(defs[i]).states;state++){
    const furni = await page.evaluate(([d,r,s]) => window.exportFurni([d],r,s), [defs[i],rotation,state]);
    for(const image of await page.evaluate(()=>window.takeFurnitureImages())){
      await writeFile(join(outDir,'furni',`f${image.id}.png`),Buffer.from(image.png,'base64'));
    }
    for (const v of furni) {
      const parts = [];
      for (const p of v.parts) {
        const { image, ...meta } = p; parts.push({ ...meta, file:`f${image}.png` });n++;
      }
      furniMeta.push({ ...v, parts });
    }
    }
    if((i+1)%40===0||i+1===defs.length)process.stdout.write(`\rfurniture: ${i+1}/${defs.length}`);
  }
  await mkdir(join(outDir, 'icons'), { recursive: true });
  for (let i = 0; i < defs.length; i += 40)
    for (const icon of await page.evaluate((d) => window.exportIcons(d), defs.slice(i, i + 40)))
      await writeFile(join(outDir, 'icons', `${icon.def}.png`), Buffer.from(icon.png, 'base64'));
  process.stdout.write('\n');
  await writeFile(join(outDir, 'furni-timelines.json'), JSON.stringify(await page.evaluate(() => window.furnitureTimelines())));
  await writeFile(join(outDir, 'furni.json'), JSON.stringify(furniMeta));
  console.log(`furniture: ${furniMeta.length} variants, ${n} layers`);

  // Shared data the packer converts for the console.
  for (const f of ['models/furni-anim.json', 'models/pets.json', 'models/pets-0.png', 'models/catalogue.json', 'public-rooms/index.json', 'classic/index.json', 'models/people.json']) {
    await mkdir(join(outDir, 'data', f, '..'), { recursive: true });
    await cp(join(snapshot, 'public', f), join(outDir, 'data', f));
  }
  for (const name of ['character-skins', 'wardrobe-extras', 'frank', 'spaces', 'status', 'badges', 'casino', 'activities', 'igloo'])
    await cp(join(snapshot, 'public', name), join(outDir, 'data', name), { recursive: true });
  await cp(join(snapshot, 'public/classic'), join(outDir, 'data/classic'), { recursive: true });
  // Catalogue and inventory icons (the same PNGs the web uses).
  await mkdir(join(outDir, 'data/icons'), { recursive: true });
  for (const f of await readdir(join(snapshot, 'public/models/catalogue')))
    if (f.endsWith('-icon.png')) await cp(join(snapshot, 'public/models/catalogue', f), join(outDir, 'data/icons', f));
  await cp(join(snapshot, 'public/public-rooms'), join(outDir, 'data/public-rooms'), { recursive: true });
  await cp(join(snapshot, 'src/wardrobe-data.json'), join(outDir, 'data/wardrobe-data.json'));
  await cp(join(snapshot, 'src/drinks.ts'), join(outDir, 'data/drinks.ts'));
  await cp(join(snapshot, 'src/pet-options.json'), join(outDir, 'data/pet-options.json'));
  await writeFile(join(outDir, 'source.json'), JSON.stringify({ web, exportedAt: new Date().toISOString() }, null, 1));
} finally {
  await browser.close();
  server.close();
  await rm(work, { recursive: true, force: true });
}
