import assert from 'node:assert/strict';
import { createHash, randomBytes, scryptSync } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawn } from 'node:child_process';
import { connect } from 'node:net';
import test from 'node:test';

const password = 'integration-password';
const deviceToken = randomBytes(24).toString('hex');
const jujuToken = randomBytes(24).toString('hex');
const papaDeviceToken = randomBytes(24).toString('hex');
const nativeTestToken = randomBytes(24).toString('hex');
const salt = randomBytes(16);
const passwordHash = `scrypt$${salt.toString('hex')}$${scryptSync(password, salt, 32).toString('hex')}`;
const dataDir = mkdtempSync(join(tmpdir(), 'supachat-test-'));
const port = 18094;
let child;
let cookie;
let nativeSessionToken;

test.before(async () => {
  child = spawn(process.execPath, ['--experimental-sqlite', 'src/server.mjs'], {
    cwd: join(import.meta.dirname, '..'),
    env: {
      ...process.env,
      SUPACHAT_HOST: '127.0.0.1', SUPACHAT_PORT: String(port), SUPACHAT_DATA_DIR: dataDir,
      SUPACHAT_PUBLIC_BASE: '/supachat', SUPACHAT_SESSION_SECRET: randomBytes(32).toString('hex'),
      SUPACHAT_PAPA_PASSWORD_HASH: passwordHash,
      SUPACHAT_ALBIE_DEVICE_TOKEN_HASH: createHash('sha256').update(deviceToken).digest('hex'),
      SUPACHAT_JUJU_DEVICE_TOKEN_HASH: createHash('sha256').update(jujuToken).digest('hex'),
      SUPACHAT_PAPA_DEVICE_TOKEN_HASH: createHash('sha256').update(papaDeviceToken).digest('hex'),
      SUPACHAT_PORTAL_HOST: 'supachat.net', SUPACHAT_PAPA_AUTHENTIK_USERNAME: 'papa',
      SUPACHAT_NATIVE_TEST_TOKEN: nativeTestToken,
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('server start timeout')), 5000);
    child.stdout.on('data', (chunk) => {
      if (String(chunk).includes('SupaChat listening')) { clearTimeout(timer); resolve(); }
    });
    child.on('exit', (code) => reject(new Error(`server exited ${code}`)));
  });
});

test.after(async () => {
  if (child && child.exitCode === null) {
    const exited = new Promise((resolve) => child.once('exit', resolve));
    child.kill();
    await exited;
  }
  rmSync(dataDir, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
});

test('health endpoint is public', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/healthz`);
  assert.equal(response.status, 200);
  assert.equal((await response.json()).service, 'supachat');
});

test('Papa signs in and receives a secure session', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/login`, {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ username: 'papa', password }),
  });
  assert.equal(response.status, 200);
  cookie = response.headers.get('set-cookie').split(';')[0];
  assert.match(response.headers.get('set-cookie'), /HttpOnly; Secure; SameSite=Strict/);
});

test('native Authentik exchange issues an isolated app session for Papa', async () => {
  const exchange = await fetch(`http://127.0.0.1:${port}/api/native/session`, {
    method: 'POST', headers: { authorization: `Bearer ${nativeTestToken}` },
  });
  assert.equal(exchange.status, 200);
  const session = await exchange.json();
  nativeSessionToken = session.token;
  assert.equal(session.user.id, 'papa');
  assert.equal(session.user.role, 'admin');
  const profile = await fetch(`http://127.0.0.1:${port}/api/session`, {
    headers: { authorization: `Bearer ${session.token}` },
  });
  assert.equal(profile.status, 200);
  assert.equal((await profile.json()).auth, 'native');
});

test('only Papa can create a one-time password setup link', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/api/admin/invitations`, {
    method: 'POST', headers: { authorization: `Bearer ${nativeSessionToken}`, 'content-type': 'application/json' },
    body: JSON.stringify({ username: 'new.friend', display_name: 'New Friend', email: 'friend@example.test' }),
  });
  assert.equal(response.status, 201);
  const invitation = await response.json();
  const invitationUrl = new URL(invitation.url);
  assert.equal(invitationUrl.origin, 'https://auth.supachat.net');
  assert.equal(invitationUrl.pathname, '/if/flow/supachat-invitation-enrollment/');
  assert.match(invitationUrl.searchParams.get('itoken'), /^[0-9a-f-]+$/);
  assert.equal(invitationUrl.searchParams.get('next'), 'https://supachat.net/?welcome=1');
  assert.equal(invitationUrl.searchParams.get('room'), 'family');
  assert.equal(invitation.room_id, 'family');

  const kbudsResponse = await fetch(`http://127.0.0.1:${port}/api/admin/invitations`, {
    method: 'POST', headers: { authorization: `Bearer ${nativeSessionToken}`, 'content-type': 'application/json' },
    body: JSON.stringify({ username: 'knowltown.friend', display_name: 'Knowlton Friend', room_id: 'k-buds' }),
  });
  assert.equal(kbudsResponse.status, 201);
  const kbudsInvitation = await kbudsResponse.json();
  assert.equal(new URL(kbudsInvitation.url).pathname, '/if/flow/supachat-invitation-enrollment/');
  assert.equal(kbudsInvitation.room_id, 'k-buds');

  const memberHeaders = { 'x-forwarded-host': 'supachat.net', 'x-authentik-uid': 'uid-member', 'x-authentik-username': 'member@example.test', 'x-authentik-name': 'Member' };
  const rejected = await fetch(`http://127.0.0.1:${port}/api/admin/invitations`, {
    method: 'POST', headers: { ...memberHeaders, origin: 'https://supachat.net', 'content-type': 'application/json' },
    body: JSON.stringify({ username: 'nope', display_name: 'Nope' }),
  });
  assert.equal(rejected.status, 403);
});

test('native app session registers an Expo notification destination', async () => {
  const exchange = await fetch(`http://127.0.0.1:${port}/api/native/session`, {
    method: 'POST', headers: { authorization: `Bearer ${nativeTestToken}` },
  }).then((response) => response.json());
  const registration = await fetch(`http://127.0.0.1:${port}/api/notifications/devices`, {
    method: 'POST',
    headers: { authorization: `Bearer ${exchange.token}`, 'content-type': 'application/json' },
    body: JSON.stringify({ provider: 'expo', token: 'ExponentPushToken[test-token]', platform: 'android', device_name: 'Test phone' }),
  });
  assert.equal(registration.status, 200);
  const rejected = await fetch(`http://127.0.0.1:${port}/api/notifications/devices`, {
    method: 'POST', headers: { cookie, 'content-type': 'application/json' },
    body: JSON.stringify({ provider: 'expo', token: 'ExponentPushToken[wrong-session]', platform: 'android' }),
  });
  assert.equal(rejected.status, 403);
});

test('an authenticated invitee claims only the room targeted by the invitation', async () => {
  const headers = { 'x-forwarded-host': 'supachat.net', 'x-authentik-uid': 'uid-friend-1', 'x-authentik-username': 'new.friend', 'x-authentik-name': 'Friendly Person', 'x-authentik-groups': 'SupaChat Family' };
  const session = await fetch(`http://127.0.0.1:${port}/api/session`, { headers });
  assert.equal(session.status, 200);
  const profile = (await session.json()).user;
  assert.match(profile.id, /^web-[0-9a-f]{16}$/);
  assert.equal(profile.display_name, 'Friendly Person');
  const sent = await fetch(`http://127.0.0.1:${port}/api/messages`, {
    method: 'POST', headers: { ...headers, origin: 'https://supachat.net', 'content-type': 'application/json' },
    body: JSON.stringify({ client_id: 'friend-test-1', body: 'hello from authentik', room_id: 'family' }),
  });
  assert.equal(sent.status, 201);
});

test('Papa creates a group and adds and removes an existing user', async () => {
  const headers = { authorization: `Bearer ${nativeSessionToken}`, 'content-type': 'application/json' };
  const created = await fetch(`http://127.0.0.1:${port}/api/admin/groups`, {method:'POST',headers,body:JSON.stringify({name:'Sunday Crew'})});
  assert.equal(created.status, 201); assert.equal((await created.json()).group.id, 'sunday-crew');
  const added = await fetch(`http://127.0.0.1:${port}/api/admin/groups/sunday-crew/members`, {method:'POST',headers,body:JSON.stringify({user_id:'albie'})});
  assert.equal(added.status, 200);
  let groups = await fetch(`http://127.0.0.1:${port}/api/admin/groups`, {headers}).then(response=>response.json());
  assert.deepEqual(groups.groups.find(group=>group.id==='sunday-crew').members.map(member=>member.id).sort(), ['albie','papa']);
  const removed = await fetch(`http://127.0.0.1:${port}/api/admin/groups/sunday-crew/members/albie`, {method:'DELETE',headers});
  assert.equal(removed.status, 200);
  groups = await fetch(`http://127.0.0.1:${port}/api/admin/groups`, {headers}).then(response=>response.json());
  assert.deepEqual(groups.groups.find(group=>group.id==='sunday-crew').members.map(member=>member.id), ['papa']);
});

test('K-BUDS membership cannot read or write Family', async () => {
  const headers = { 'x-forwarded-host': 'supachat.net', 'x-authentik-uid': 'uid-kbuds-1', 'x-authentik-username': 'knowltown.friend', 'x-authentik-name': 'Knowlton Friend', 'x-authentik-groups': 'SupaChat K-BUDS' };
  const session = await fetch(`http://127.0.0.1:${port}/api/session`, { headers }).then((response) => response.json());
  assert.deepEqual(session.rooms.map((room) => room.id), ['k-buds']);
  assert.equal((await fetch(`http://127.0.0.1:${port}/api/messages?room=family`, { headers })).status, 403);
  const sent = await fetch(`http://127.0.0.1:${port}/api/messages`, { method:'POST', headers:{...headers,origin:'https://supachat.net','content-type':'application/json'}, body:JSON.stringify({client_id:'kbuds-only-1',body:'hello K-BUDS',room_id:'k-buds'}) });
  assert.equal(sent.status, 201);
  const family = await fetch(`http://127.0.0.1:${port}/api/messages?room=family`, {headers:{cookie}}).then((response) => response.json());
  assert.equal(family.messages.some((message) => message.client_id === 'kbuds-only-1'), false);
});

test('Papa Authentik identity maps to existing Papa history', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/api/session`, {
    headers: { 'x-forwarded-host': 'supachat.net', 'x-authentik-uid': 'uid-papa', 'x-authentik-username': 'papa', 'x-authentik-name': 'Papa' },
  });
  assert.equal((await response.json()).user.id, 'papa');
});

test('SupaChat-native Authentik usernames preserve family identities', async () => {
  for (const [username, expectedId] of [['papa', 'papa'], ['albie', 'albie'], ['julien', 'juju']]) {
    const response = await fetch(`http://127.0.0.1:${port}/api/session`, {
      headers: {
        'x-forwarded-host': 'supachat.net',
        'x-authentik-uid': `uid-${username}`,
        'x-authentik-username': username,
        'x-authentik-name': username[0].toUpperCase() + username.slice(1),
      },
    });
    assert.equal(response.status, 200);
    assert.equal((await response.json()).user.id, expectedId);
  }
});

test('Authentik headers are rejected outside the configured portal host', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/api/session`, {
    headers: { 'x-forwarded-host': 'le954.ca', 'x-authentik-uid': 'spoofed', 'x-authentik-username': 'attacker@example.test' },
  });
  assert.equal(response.status, 401);
});

test('Papa sends a 140-character message idempotently', async () => {
  const payload = { client_id: 'papa-test-1', body: 'x'.repeat(140) };
  const send = () => fetch(`http://127.0.0.1:${port}/api/messages`, {
    method: 'POST', headers: { cookie, 'content-type': 'application/json' }, body: JSON.stringify(payload),
  });
  assert.equal((await send()).status, 201);
  assert.equal((await send()).status, 200);
  const history = await fetch(`http://127.0.0.1:${port}/api/messages`, { headers: { cookie } }).then((r) => r.json());
  assert.equal(history.messages.filter((message) => message.client_id === payload.client_id).length, 1);
});

test('messages over 140 characters are rejected', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/api/messages`, {
    method: 'POST', headers: { cookie, 'content-type': 'application/json' },
    body: JSON.stringify({ client_id: 'too-long', body: 'x'.repeat(141) }),
  });
  assert.equal(response.status, 400);
});

test('Albie synchronizes and records delivery and read state', async () => {
  const headers = { authorization: `Bearer ${deviceToken}` };
  const sync = await fetch(`http://127.0.0.1:${port}/api/device/sync?after=0`, { headers }).then((r) => r.json());
  const papaMessage = sync.messages.find((message) => message.client_id === 'papa-test-1');
  assert.equal(papaMessage.body.length, 140);
  const receipt = await fetch(`http://127.0.0.1:${port}/api/receipts`, {
    method: 'POST', headers: { ...headers, 'content-type': 'application/json' },
    body: JSON.stringify({ message_id: papaMessage.id, state: 'read' }),
  });
  assert.equal(receipt.status, 200);
  const history = await fetch(`http://127.0.0.1:${port}/api/messages`, { headers: { cookie } }).then((r) => r.json());
  assert.equal(history.messages.find((message) => message.id === papaMessage.id).receipts.find((item) => item.user_id === 'albie').state, 'read');
});

test('device sync honors bounded message and receipt pages', async () => {
  const headers = { authorization: `Bearer ${deviceToken}` };
  const sync = await fetch(`http://127.0.0.1:${port}/api/device/sync?after=0&receipts_after=0&limit=1`, { headers }).then((r) => r.json());
  assert.ok(sync.messages.length <= 1);
  assert.ok(sync.receipts.length <= 1);
});

test('device sync can disable long polling for responsive voice controls', async () => {
  const headers = { authorization: `Bearer ${deviceToken}` };
  const started = Date.now();
  const response = await fetch(`http://127.0.0.1:${port}/api/device/sync?after=999999&receipts_after=9999999999999&limit=20&wait=0`, { headers });
  assert.equal(response.status, 200);
  assert.ok(Date.now() - started < 1000);
});

test('Albie can send a message to Papa', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/api/messages`, {
    method: 'POST', headers: { authorization: `Bearer ${deviceToken}`, 'content-type': 'application/json' },
    body: JSON.stringify({ client_id: 'albie-test-1', body: 'hello papa' }),
  });
  assert.equal(response.status, 201);
  const history = await fetch(`http://127.0.0.1:${port}/api/messages`, { headers: { cookie } }).then((r) => r.json());
  assert.equal(history.messages.at(-1).author_id, 'albie');
  assert.equal(history.messages.at(-1).receipts.find((item) => item.user_id === 'papa').state, 'read');
});

test('Juju has an independent device identity and shared-family sync', async () => {
  const headers = { authorization: `Bearer ${jujuToken}` };
  const sync = await fetch(`http://127.0.0.1:${port}/api/device/sync?after=0`, { headers }).then((r) => r.json());
  assert.ok(sync.messages.length >= 2);
  const response = await fetch(`http://127.0.0.1:${port}/api/messages`, {
    method: 'POST', headers: { ...headers, 'content-type': 'application/json' },
    body: JSON.stringify({ client_id: 'juju-test-1', body: 'hello family' }),
  });
  assert.equal(response.status, 201);
  const history = await fetch(`http://127.0.0.1:${port}/api/messages`, { headers: { cookie } }).then((r) => r.json());
  assert.equal(history.messages.at(-1).author_id, 'juju');
});

test('Papa Cardputer uses bearer sync without changing Papa web login', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/api/device/sync?after=0&wait=0`, {
    headers: { authorization: `Bearer ${papaDeviceToken}` },
  });
  assert.equal(response.status, 200);
  const payload = await response.json();
  assert.ok(Array.isArray(payload.messages));
});

test('Papa uploads and retrieves a bounded PCM voice clip', async () => {
  const pcm = Buffer.alloc(1600);
  for (let index = 0; index < pcm.length; index += 2) pcm.writeInt16LE((index * 17) % 32767, index);
  const upload = await fetch(`http://127.0.0.1:${port}/api/voice`, {
    method: 'POST', headers: { cookie, 'content-type': 'application/octet-stream', 'x-client-id': 'voice-test-1', 'x-sample-rate': '8000' }, body: pcm,
  });
  assert.equal(upload.status, 201);
  const message = (await upload.json()).message;
  assert.equal(message.type, 'voice');
  assert.equal(message.voice.sample_count, 800);
  const audio = await fetch(`http://127.0.0.1:${port}/api/voice/${message.id}/audio`, { headers: { cookie } });
  assert.equal(audio.status, 200);
  assert.equal(Buffer.compare(Buffer.from(await audio.arrayBuffer()), pcm), 0);
  const wav = await fetch(`http://127.0.0.1:${port}/api/voice/${message.id}/audio?format=wav`, { headers: { cookie } });
  assert.equal(wav.status, 200);
  assert.equal(wav.headers.get('content-type'), 'audio/wav');
  const wavBytes = Buffer.from(await wav.arrayBuffer());
  assert.equal(wavBytes.subarray(0, 4).toString(), 'RIFF');
  assert.equal(wavBytes.subarray(8, 12).toString(), 'WAVE');
  assert.equal(Buffer.compare(wavBytes.subarray(44), pcm), 0);
});

test('voice clips reject oversized or malformed audio', async () => {
  const response = await fetch(`http://127.0.0.1:${port}/api/voice`, {
    method: 'POST', headers: { cookie, 'content-type': 'application/octet-stream', 'x-client-id': 'bad-voice', 'x-sample-rate': '8000' }, body: Buffer.alloc(3),
  });
  assert.equal(response.status, 400);
});

test('authenticated device obtains and releases the half-duplex walkie channel', async () => {
  await new Promise((resolve, reject) => {
    const socket = connect(port, '127.0.0.1'); let received = Buffer.alloc(0); let upgraded = false;
    const timer = setTimeout(() => { socket.destroy(); reject(new Error('walkie timeout')); }, 3000);
    socket.on('connect', () => socket.write([
      'GET /walkie HTTP/1.1', `Host: 127.0.0.1:${port}`, 'Upgrade: websocket', 'Connection: Upgrade',
      'Sec-WebSocket-Key: dGVzdC13YWxraWUta2V5', 'Sec-WebSocket-Version: 13', `Authorization: Bearer ${deviceToken}`, '', '',
    ].join('\r\n')));
    socket.on('data', (chunk) => {
      received = Buffer.concat([received, chunk]);
      if (!upgraded && received.includes(Buffer.from('\r\n\r\n'))) {
        assert.match(received.toString(), /101 Switching Protocols/); upgraded = true; received = Buffer.alloc(0);
        const payload = Buffer.from(JSON.stringify({ type:'ptt_start' })); const mask = Buffer.from([1,2,3,4]);
        const frame = Buffer.alloc(6 + payload.length); frame[0] = 0x81; frame[1] = 0x80 | payload.length; mask.copy(frame, 2);
        for (let index = 0; index < payload.length; index++) frame[6 + index] = payload[index] ^ mask[index % 4];
        socket.write(frame);
      } else if (upgraded && received.includes(Buffer.from('ptt_start'))) {
        clearTimeout(timer); socket.destroy(); resolve();
      }
    });
    socket.on('error', reject);
  });
});

test('walkie rejects an unauthenticated WebSocket upgrade', async () => {
  await new Promise((resolve, reject) => {
    const socket = connect(port, '127.0.0.1'); let received = '';
    const timer = setTimeout(() => { socket.destroy(); reject(new Error('walkie rejection timeout')); }, 3000);
    socket.on('connect', () => socket.write([
      'GET /walkie HTTP/1.1', `Host: 127.0.0.1:${port}`, 'Upgrade: websocket', 'Connection: Upgrade',
      'Sec-WebSocket-Key: dW5hdXRoZW50aWNhdGVk', 'Sec-WebSocket-Version: 13', '', '',
    ].join('\r\n')));
    socket.on('data', (chunk) => {
      received += chunk;
      if (!received.includes('\r\n\r\n')) return;
      clearTimeout(timer); assert.match(received, /401 Unauthorized/); socket.destroy(); resolve();
    });
    socket.on('error', reject);
  });
});
