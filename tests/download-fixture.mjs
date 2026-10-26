import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {createServer as createSecureServer} from 'node:https';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';

const size = process.env.BENCH ? 8 * 1024 * 1024 + 37 : 2 * 1024 * 1024 + 37;
const payload = Buffer.alloc(size);
for (let i = 0; i < size; i++) payload[i] = (i * 31 + 7) & 255;
const folder = await mkdtemp(path.join(tmpdir(), 'tweebbo-download-'));
const server = process.env.TLS_CERT ? createSecureServer({cert: await readFile(process.env.TLS_CERT), key: await readFile(process.env.TLS_KEY)}) : createServer();
const sockets = new Set();
let requests = [], transferred = 0, connections = 0;
server.on('connection', socket => {
  connections++; sockets.add(socket); socket.on('error', () => {}); socket.on('close', () => sockets.delete(socket));
});
server.on('request', (req, res) => {
  requests.push({url: req.url, range: req.headers.range, validator: req.headers['if-range']});
  const mode = req.url.slice(1);
  if (mode === 'redirect') { res.writeHead(302, {Location: '/normal', 'Content-Length': 0}); res.end(); return; }
  let first = 0, last = size - 1, status = 200;
  const match = /bytes=(\d+)-(\d*)/.exec(req.headers.range || '');
  if (match && mode !== 'ignore') {
    first = Number(match[1]); last = match[2] ? Math.min(Number(match[2]), last) : last; status = 206;
  }
  const probe = first === 0 && last === 0;
  const headers = {'Content-Length': last - first + 1, ETag: '"fixture-v1"', 'Accept-Ranges': 'bytes'};
  if (mode === 'no-validator') delete headers.ETag;
  if (mode === 'date') { delete headers.ETag; headers['Last-Modified'] = 'Sun, 25 Oct 2026 20:00:00 GMT'; }
  if (status === 206) headers['Content-Range'] = `bytes ${mode === 'bad-range' ? first + 1 : first}-${last}/${size}`;
  if (mode === 'changed' && !probe && match?.[2]) headers.ETag = '"fixture-v2"';
  if (mode === 'close') headers.Connection = 'close';
  if (mode === 'bad-length') headers['Content-Length'] += 100;
  res.writeHead(status, headers);
  if (mode === 'cut' && !probe && match?.[2]) { res.write(payload.subarray(first, first + 8000)); setTimeout(() => res.destroy(), 5); return; }
  if (mode === 'bad-length') { res.end(payload.subarray(first, last + 1)); setTimeout(() => res.destroy(), 5); return; }
  if (mode === 'slow' && !probe) {
    let offset = first;
    const send = () => {
      if (res.destroyed) return;
      const end = Math.min(offset + 16384, last + 1);
      res.write(payload.subarray(offset, end)); transferred += end - offset; offset = end;
      if (offset > last) res.end(); else setTimeout(send, 32);
    };
    setTimeout(send, 15);
  } else { transferred += last - first + 1; res.end(payload.subarray(first, last + 1)); }
});
server.keepAliveTimeout = 60000;
server.listen(0, '127.0.0.1');
await once(server, 'listening');
const report = [];
async function run(mode, parallel, from = 0, stop = 0) {
  requests = []; transferred = 0; connections = 0;
  const destination = path.join(folder, 'result.bin');
  const exe = process.env.DOWNLOAD_TEST || new URL('./download_test', import.meta.url).pathname;
  const child = spawn(exe, [`${process.env.TLS_CERT ? 'https' : 'http'}://${process.env.TLS_HOST || '127.0.0.1'}:${server.address().port}/${mode}`, String(parallel), String(from), String(stop), destination]);
  let output = '', error = '';
  child.stdout.on('data', b => output += b); child.stderr.on('data', b => error += b);
  const timer = setTimeout(() => child.kill('SIGKILL'), 60000);
  const [code] = await once(child, 'exit'); clearTimeout(timer);
  assert.equal(code, 0, `${mode}: ${error} ${output}`);
  const result = JSON.parse(output.trim());
  if (stop || mode === 'bad-length') assert.equal(result.status, 0, mode);
  else {
    assert([200,206].includes(result.status), `${mode}: ${error} ${output}`);
    assert.equal(result.offset, size); assert.equal(result.total, size);
    assert.deepEqual(await readFile(destination), payload.subarray(from));
  }
  if (!stop && ['normal','date','slow'].includes(mode) && parallel > 1 && size - from >= 1048576) {
    assert.equal(connections, parallel, 'Persistent sockets must be reused for later pieces');
    assert.equal(transferred, size - from + 1, 'Only the probe adds a byte');
    assert(requests.slice(1).every(r => r.validator === (mode === 'date' ? 'Sun, 25 Oct 2026 20:00:00 GMT' : '"fixture-v1"')));
  }
  const record = {mode, parallel, from, ...result, connections, requests: requests.length, transferred};
  report.push(record); console.log(JSON.stringify(record));
}
try {
  if (process.env.BENCH) {
    for (let round = 0; round < 2; round++) for (const n of [1,2,4]) await run('slow', n);
  } else {
    for (const n of [1,2,4]) await run('normal', n);
    await run('normal', 2, 314573);
    await run('normal', 2, size - 32768);
    for (const mode of ['ignore','no-validator','date','bad-range','changed','cut','close','redirect','bad-length']) await run(mode, 2);
    await run('normal', 2, 0, 524288);
  }
  if (process.env.REPORT) await writeFile(process.env.REPORT, JSON.stringify(report, null, 2) + '\n');
} finally {
  for (const socket of sockets) socket.destroy();
  await new Promise(resolve => server.close(resolve));
  await rm(folder, {recursive: true, force: true});
}
