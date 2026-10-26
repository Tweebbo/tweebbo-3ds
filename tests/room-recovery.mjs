import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';
import {once} from 'node:events';

for (const mode of ['recover', 'forbidden', 'server-error', 'disconnect', 'missing-reception', 'late']) {
  const paths = [], sockets = new Set();
  const server = createServer();
  server.on('upgrade', (req, socket) => {
    sockets.add(socket);
    socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
    paths.push(req.url);
    if (mode === 'disconnect') { socket.destroy(); return; }
    if (req.url === '/ws/deleted-room' || mode === 'missing-reception') {
      const status = mode === 'forbidden' ? 403 : mode === 'server-error' ? 500 : 404;
      socket.end(`HTTP/1.1 ${status} Rejected\r\nContent-Length: 0\r\nConnection: close\r\n\r\n`);
      return;
    }
    const accept = createHash('sha1').update(req.headers['sec-websocket-key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    let pending = Buffer.alloc(0), welcomed = false;
    socket.on('data', chunk => {
      pending = Buffer.concat([pending, chunk]);
      if (welcomed || pending.length < 6) return;
      const length = pending[1] & 127;
      assert(length < 126);
      if (pending.length < length + 6) return;
      const mask = pending.subarray(2, 6), hello = Buffer.from(pending.subarray(6, 6 + length));
      for (let i = 0; i < hello.length; i++) hello[i] ^= mask[i % 4];
      assert.equal(hello[0], 1);
      assert.equal(hello.subarray(4, 4 + hello[3]).toString(), 'fixture-identity');
      const str = s => [Buffer.byteLength(s), ...Buffer.from(s)];
      const body = Buffer.from([101, 1, ...str('Fixture'), ...str('fixture-identity'), 200, 1, 1]);
      socket.write(Buffer.concat([Buffer.from([0x82, body.length]), body]));
      welcomed = true;
    });
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const child = spawn(new URL('./room-recovery', import.meta.url).pathname,
    [`ws://127.0.0.1:${server.address().port}`, mode], {stdio: ['ignore', 'pipe', 'pipe']});
  let output = '';
  child.stdout.on('data', b => output += b);
  child.stderr.on('data', b => output += b);
  const timer = setTimeout(() => child.kill('SIGKILL'), 8000);
  const [code] = await once(child, 'exit');
  clearTimeout(timer);
  for (const socket of sockets) socket.destroy();
  await new Promise(resolve => server.close(resolve));
  assert.equal(code, 0, `${mode}: ${output}`);
  assert.deepEqual(paths, ['/ws/deleted-room', ...(
    mode === 'late' ? ['/ws/park'] : ['recover', 'missing-reception'].includes(mode) ? ['/ws/lobby-es'] : [])], mode);
  console.log(`${mode}: passed`);
}
