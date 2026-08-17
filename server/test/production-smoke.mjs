import assert from 'node:assert/strict';
import { createHash, randomBytes } from 'node:crypto';
import { connect } from 'node:tls';

const token = process.env.SUPACHAT_DEVICE_TOKEN;
const host = process.env.SUPACHAT_SMOKE_HOST || 'supachat.net';
const path = process.env.SUPACHAT_SMOKE_PATH || '/walkie?room=family';
if (!token) throw new Error('SUPACHAT_DEVICE_TOKEN is required');

function clientTextFrame(value) {
  const payload = Buffer.from(JSON.stringify(value));
  assert.ok(payload.length < 126);
  const mask = randomBytes(4);
  const frame = Buffer.alloc(6 + payload.length);
  frame[0] = 0x81; frame[1] = 0x80 | payload.length; mask.copy(frame, 2);
  for (let index = 0; index < payload.length; index++) frame[index + 6] = payload[index] ^ mask[index % 4];
  return frame;
}

const key = randomBytes(16).toString('base64');
const expectedAccept = createHash('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
const socket = connect({ host, port: 443, servername: host });
const result = await new Promise((resolve, reject) => {
  let bytes = Buffer.alloc(0); let upgraded = false; let started = false;
  const timer = setTimeout(() => reject(new Error('production walkie smoke timeout')), 10_000);
  const finish = (error) => {
    clearTimeout(timer); socket.destroy(); error ? reject(error) : resolve({ upgraded, started });
  };
  socket.on('secureConnect', () => socket.write([
    `GET ${path} HTTP/1.1`, `Host: ${host}`, 'Upgrade: websocket', 'Connection: Upgrade',
    `Sec-WebSocket-Key: ${key}`, 'Sec-WebSocket-Version: 13', `Authorization: Bearer ${token}`, '', '',
  ].join('\r\n')));
  socket.on('data', (chunk) => {
    try {
      bytes = Buffer.concat([bytes, chunk]);
      if (!upgraded) {
        const headerEnd = bytes.indexOf('\r\n\r\n'); if (headerEnd < 0) return;
        const headers = bytes.subarray(0, headerEnd).toString();
        assert.match(headers, /^HTTP\/1\.1 101 /); assert.ok(headers.toLowerCase().includes(expectedAccept.toLowerCase()));
        upgraded = true; bytes = bytes.subarray(headerEnd + 4); socket.write(clientTextFrame({ type: 'ptt_start' }));
      }
      while (bytes.length >= 2) {
        let length = bytes[1] & 0x7f; let offset = 2;
        if (length === 126) { if (bytes.length < 4) return; length = bytes.readUInt16BE(2); offset = 4; }
        if (bytes.length < offset + length) return;
        const opcode = bytes[0] & 0x0f; const payload = bytes.subarray(offset, offset + length); bytes = bytes.subarray(offset + length);
        if (opcode !== 1) continue;
        const message = JSON.parse(payload);
        if (message.type === 'ptt_start') {
          started = true; socket.write(clientTextFrame({ type: 'ptt_stop' })); finish(); return;
        }
      }
    } catch (error) { finish(error); }
  });
  socket.on('error', finish);
});

console.log(`production walkie smoke passed: tls=${result.upgraded} ptt=${result.started}`);
