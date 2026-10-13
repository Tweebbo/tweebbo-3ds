// Draws avatars with the web code (snapshot) for the icon and the banner: build/brand/*.png
import { cp, mkdir, rm, writeFile, readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { join, resolve, extname } from 'node:path';
import { homedir, tmpdir } from 'node:os';
import { execFileSync } from 'node:child_process';
import { chromium } from 'playwright-core';

const here = import.meta.dirname;
const out = resolve(join(here, '../../build/brand'));
const web = resolve(process.env.TWEEBBO_WEB ?? join(homedir(), 'Tweebbo-Vercel'));
const work = join(tmpdir(), `tweebbo-brand-${process.pid}`);
const snapshot = join(work, 'web');
await mkdir(snapshot, { recursive: true });
await cp(join(web, 'client/src'), join(snapshot, 'src'), { recursive: true });
await cp(join(web, 'client/public'), join(snapshot, 'public'), { recursive: true, filter: (p) => !p.includes('/3ds') });
await cp(join(web, 'client/tsconfig.json'), join(snapshot, 'tsconfig.json'));
execFileSync(join(here, 'node_modules/.bin/vite'), ['build', '--config', join(here, 'vite.config.mjs')], {
  stdio: 'inherit', env: { ...process.env, TWEEBBO_SNAPSHOT: snapshot, TWEEBBO_EXPORT_BUILD: join(work, 'build') },
});
const server = createServer(async (req, res) => {
  const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  try {
    let file;
    if (path === '/') { res.setHeader('content-type', 'text/html'); return res.end('<!doctype html><script type="module" src="/export.js"></script>'); }
    if (path === '/export.js') file = join(work, 'build/export.js');
    else if (path.startsWith('/static/')) file = join(snapshot, 'public', path.slice(8));
    res.setHeader('content-type', { '.js': 'text/javascript', '.json': 'application/json', '.png': 'image/png' }[extname(file)] ?? 'application/octet-stream');
    res.end(await readFile(file));
  } catch { res.statusCode = 404; res.end(); }
});
await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true });
try {
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => typeof window.exportAvatars === 'function');
  const looks = JSON.parse(process.argv[2]);
  const pngs = await page.evaluate((l) => window.exportAvatars(l), looks);
  await rm(out, { recursive: true, force: true });
  await mkdir(out, { recursive: true });
  for (const [i, p] of pngs.entries()) await writeFile(join(out, `avatar${i}.png`), Buffer.from(p, 'base64'));
  console.log(`brand: ${pngs.length} avatars`);
} finally { await browser.close(); server.close(); await rm(work, { recursive: true, force: true }); }
