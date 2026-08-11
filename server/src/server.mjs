import { createHash, createHmac, randomBytes, scryptSync, timingSafeEqual } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { DatabaseSync } from 'node:sqlite';

const sourceRoot = dirname(fileURLToPath(import.meta.url));
const webRoot = join(sourceRoot, '..', 'web');
const config = {
  host: process.env.SUPACHAT_HOST || '127.0.0.1',
  port: Number(process.env.SUPACHAT_PORT || 8094),
  dataDir: process.env.SUPACHAT_DATA_DIR || join(sourceRoot, '..', 'data'),
  publicBase: (process.env.SUPACHAT_PUBLIC_BASE || '/supachat').replace(/\/$/, ''),
  portalHost: (process.env.SUPACHAT_PORTAL_HOST || 'supachat.net').toLowerCase(),
  papaAuthentikUsername: (process.env.SUPACHAT_PAPA_AUTHENTIK_USERNAME || '').toLowerCase(),
  authentikUserinfoUrl: process.env.SUPACHAT_AUTHENTIK_USERINFO_URL || 'https://auth.supachat.net/application/o/userinfo/',
  mobileClientId: process.env.SUPACHAT_MOBILE_CLIENT_ID || 'supachat-android',
  nativeTestToken: process.env.SUPACHAT_NATIVE_TEST_TOKEN || '',
  expoPushEnabled: process.env.SUPACHAT_EXPO_PUSH_ENABLED === 'true',
  sessionSecret: process.env.SUPACHAT_SESSION_SECRET || '',
  papaPasswordHash: process.env.SUPACHAT_PAPA_PASSWORD_HASH || '',
  deviceTokenHashes: {
    albie: process.env.SUPACHAT_ALBIE_DEVICE_TOKEN_HASH || '',
    juju: process.env.SUPACHAT_JUJU_DEVICE_TOKEN_HASH || '',
    papa: process.env.SUPACHAT_PAPA_DEVICE_TOKEN_HASH || '',
  },
};

const authentikIdentityMap = new Map([
  ['papa', 'papa'],
  ['albie', 'albie'],
  ['julien', 'juju'],
]);
if (config.papaAuthentikUsername) authentikIdentityMap.set(config.papaAuthentikUsername, 'papa');

if (config.sessionSecret.length < 32 || !config.papaPasswordHash || !config.deviceTokenHashes.albie) {
  throw new Error('SUPACHAT_SESSION_SECRET, SUPACHAT_PAPA_PASSWORD_HASH, and SUPACHAT_ALBIE_DEVICE_TOKEN_HASH are required');
}

mkdirSync(config.dataDir, { recursive: true });
const voiceDir = join(config.dataDir, 'voice');
mkdirSync(voiceDir, { recursive: true });
const db = new DatabaseSync(join(config.dataDir, 'supachat.sqlite'));
db.exec(`
  PRAGMA journal_mode=WAL;
  PRAGMA foreign_keys=ON;
  CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    short_name TEXT NOT NULL,
    kind TEXT NOT NULL CHECK (kind IN ('web', 'device')),
    revoked_at INTEGER
  );
  CREATE TABLE IF NOT EXISTS conversations (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    kind TEXT NOT NULL CHECK (kind IN ('shared', 'room', 'direct'))
  );
  CREATE TABLE IF NOT EXISTS conversation_members (
    conversation_id TEXT NOT NULL REFERENCES conversations(id),
    user_id TEXT NOT NULL REFERENCES users(id),
    PRIMARY KEY (conversation_id, user_id)
  );
  CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL REFERENCES conversations(id),
    author_id TEXT NOT NULL REFERENCES users(id),
    client_id TEXT NOT NULL,
    type TEXT NOT NULL DEFAULT 'text',
    body TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    UNIQUE (author_id, client_id)
  );
  CREATE TABLE IF NOT EXISTS receipts (
    message_id INTEGER NOT NULL REFERENCES messages(id),
    user_id TEXT NOT NULL REFERENCES users(id),
    state TEXT NOT NULL CHECK (state IN ('server', 'delivered', 'read')),
    updated_at INTEGER NOT NULL,
    PRIMARY KEY (message_id, user_id)
  );
  CREATE TABLE IF NOT EXISTS presence (
    user_id TEXT PRIMARY KEY REFERENCES users(id),
    last_seen_at INTEGER NOT NULL,
    connected INTEGER NOT NULL DEFAULT 0
  );
  CREATE TABLE IF NOT EXISTS voice_clips (
    message_id INTEGER PRIMARY KEY REFERENCES messages(id),
    file_name TEXT NOT NULL UNIQUE,
    mime_type TEXT NOT NULL,
    sample_rate INTEGER NOT NULL,
    sample_count INTEGER NOT NULL,
    duration_ms INTEGER NOT NULL,
    byte_length INTEGER NOT NULL
  );
  CREATE TABLE IF NOT EXISTS notification_devices (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL REFERENCES users(id),
    provider TEXT NOT NULL CHECK (provider IN ('expo', 'webpush')),
    token TEXT NOT NULL UNIQUE,
    platform TEXT NOT NULL,
    device_name TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    last_error TEXT
  );
  CREATE INDEX IF NOT EXISTS idx_messages_conversation_id ON messages(conversation_id, id);
`);
try { db.exec("ALTER TABLE users ADD COLUMN role TEXT NOT NULL DEFAULT 'member' CHECK (role IN ('admin', 'member'))"); } catch (error) {
  if (!String(error).includes('duplicate column')) throw error;
}

const seed = db.prepare('INSERT OR IGNORE INTO users(id, display_name, short_name, kind) VALUES (?, ?, ?, ?)');
seed.run('papa', 'Papa', 'Papa', 'web');
seed.run('albie', 'Albie', 'Albie', 'device');
seed.run('juju', 'Julien', 'Juju', 'device');
db.prepare("UPDATE users SET role = 'admin' WHERE id = 'papa'").run();
db.prepare("INSERT OR IGNORE INTO conversations(id, name, kind) VALUES ('family', 'Family', 'shared')").run();
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('papa');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('albie');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('juju');

const clients = new Set();
const longPolls = new Set();
const walkieClients = new Map();
let walkieSpeaker = null;
let walkieStartedAt = 0;
const loginAttempts = new Map();
const assets = new Map([
  ['/app.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'app.css'))]],
  ['/controls.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'controls.css'))]],
  ['/app.js', ['text/javascript; charset=utf-8', readFileSync(join(webRoot, 'app.js'))]],
  ['/supachat-logo.png', ['image/png', readFileSync(join(webRoot, 'supachat-logo.png'))]],
]);
const loginHtml = readFileSync(join(webRoot, 'login.html'));
const appHtml = readFileSync(join(webRoot, 'index.html'));

function json(res, status, value, headers = {}) {
  res.writeHead(status, { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store', ...headers });
  res.end(JSON.stringify(value));
}

function parseCookies(req) {
  return Object.fromEntries((req.headers.cookie || '').split(';').filter(Boolean).map((part) => {
    const at = part.indexOf('=');
    return [part.slice(0, at).trim(), decodeURIComponent(part.slice(at + 1))];
  }));
}

function signSession(payload) {
  const encoded = Buffer.from(JSON.stringify(payload)).toString('base64url');
  const signature = createHmac('sha256', config.sessionSecret).update(encoded).digest('base64url');
  return `${encoded}.${signature}`;
}

function verifySignedSession(token, expectedKind = null) {
  if (!token) return null;
  const [encoded, signature] = token.split('.');
  if (!encoded || !signature) return null;
  const expected = createHmac('sha256', config.sessionSecret).update(encoded).digest();
  let actual;
  try { actual = Buffer.from(signature, 'base64url'); } catch { return null; }
  if (actual.length !== expected.length || !timingSafeEqual(actual, expected)) return null;
  try {
    const payload = JSON.parse(Buffer.from(encoded, 'base64url').toString());
    if (!payload.user || payload.exp <= Date.now() || (expectedKind && payload.kind !== expectedKind)) return null;
    const row = db.prepare('SELECT revoked_at FROM users WHERE id = ?').get(payload.user);
    return row && !row.revoked_at ? payload.user : null;
  } catch { return null; }
}

function requestHost(req) {
  return String(req.headers['x-forwarded-host'] || req.headers.host || '').split(':')[0].toLowerCase();
}

function authentikUserId(username, uid) {
  return authentikIdentityMap.get(String(username).toLowerCase())
    || `web-${createHash('sha256').update(String(uid)).digest('hex').slice(0, 16)}`;
}

function webUser(req) {
  const host = requestHost(req);
  const authentikUid = String(req.headers['x-authentik-uid'] || '').trim();
  const authentikUsername = String(req.headers['x-authentik-username'] || '').trim();
  if (host === config.portalHost && authentikUid && authentikUsername) {
    const normalizedUsername = authentikUsername.toLowerCase();
    const id = authentikUserId(normalizedUsername, authentikUid);
    const displayName = String(req.headers['x-authentik-name'] || authentikUsername).trim().slice(0, 80) || authentikUsername.slice(0, 80);
    const shortName = displayName.split(/\s+/)[0].slice(0, 12) || 'Friend';
    db.prepare(`
      INSERT INTO users(id, display_name, short_name, kind) VALUES (?, ?, ?, 'web')
      ON CONFLICT(id) DO UPDATE SET display_name = excluded.display_name, short_name = excluded.short_name
    `).run(id, displayName, shortName);
    db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run(id);
    return id;
  }
  return verifySignedSession(parseCookies(req).supachat_session);
}

function nativeUser(req) {
  const value = String(req.headers.authorization || '');
  return value.startsWith('Bearer ') ? verifySignedSession(value.slice(7), 'native') : null;
}

function deviceUser(req) {
  const value = req.headers.authorization || '';
  if (!value.startsWith('Bearer ')) return null;
  const digest = createHash('sha256').update(value.slice(7)).digest('hex');
  const actual = Buffer.from(digest);
  for (const [userId, tokenHash] of Object.entries(config.deviceTokenHashes)) {
    if (!tokenHash) continue;
    const expected = Buffer.from(tokenHash);
    if (actual.length !== expected.length || !timingSafeEqual(actual, expected)) continue;
    const row = db.prepare('SELECT revoked_at FROM users WHERE id = ?').get(userId);
    return row && !row.revoked_at ? userId : null;
  }
  return null;
}

function actor(req) {
  return webUser(req) || nativeUser(req) || deviceUser(req);
}

function jwtAudience(token) {
  try {
    const payload = JSON.parse(Buffer.from(token.split('.')[1], 'base64url').toString());
    return Array.isArray(payload.aud) ? payload.aud : [payload.aud];
  } catch { return []; }
}

async function authentikProfile(accessToken) {
  if (config.nativeTestToken && accessToken === config.nativeTestToken) {
    return { sub: 'native-test-papa', preferred_username: config.papaAuthentikUsername || 'papa@example.test', name: 'Papa' };
  }
  if (!config.mobileClientId || !jwtAudience(accessToken).includes(config.mobileClientId)) return null;
  const response = await fetch(config.authentikUserinfoUrl, { headers: { authorization: `Bearer ${accessToken}` } });
  if (!response.ok) return null;
  const profile = await response.json();
  return profile?.sub && (profile.preferred_username || profile.email) ? profile : null;
}

async function sendExpoNotifications(message) {
  if (!config.expoPushEnabled) return;
  const devices = db.prepare("SELECT id, token FROM notification_devices WHERE provider = 'expo' AND enabled = 1 AND user_id <> ?").all(message.author_id);
  if (!devices.length) return;
  const payloads = devices.map((device) => ({
    to: device.token,
    sound: 'default',
    title: `${message.author_name} · Family`,
    body: message.type === 'voice' ? 'Sent a voice message' : message.body,
    data: { conversation_id: message.conversation_id, message_id: message.id },
    channelId: 'messages',
  }));
  try {
    const response = await fetch('https://exp.host/--/api/v2/push/send', {
      method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(payloads),
    });
    if (!response.ok) throw new Error(`expo_push_${response.status}`);
  } catch (error) {
    console.error('Expo push delivery failed', error);
  }
}

function requestBase(req) {
  return requestHost(req) === config.portalHost ? '' : config.publicBase;
}

async function body(req, limit = 4096) {
  const chunks = [];
  let length = 0;
  for await (const chunk of req) {
    length += chunk.length;
    if (length > limit) throw new Error('body_too_large');
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
}

async function rawBody(req, limit = 96_000) {
  const chunks = []; let length = 0;
  for await (const chunk of req) {
    length += chunk.length;
    if (length > limit) throw new Error('body_too_large');
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

function pcmWav(pcm, sampleRate = 8000) {
  const header = Buffer.alloc(44);
  header.write('RIFF', 0); header.writeUInt32LE(36 + pcm.length, 4); header.write('WAVE', 8);
  header.write('fmt ', 12); header.writeUInt32LE(16, 16); header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22); header.writeUInt32LE(sampleRate, 24); header.writeUInt32LE(sampleRate * 2, 28);
  header.writeUInt16LE(2, 32); header.writeUInt16LE(16, 34); header.write('data', 36); header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}

function messageRows(after = 0, limit = 100) {
  const bounded = Math.min(Math.max(limit, 1), 100);
  const rows = after > 0 ? db.prepare(`
    SELECT m.id, m.client_id, m.conversation_id, m.author_id, u.display_name AS author_name,
           u.short_name AS author_short, m.type, m.body, m.created_at
    FROM messages m JOIN users u ON u.id = m.author_id
    WHERE m.conversation_id = 'family' AND m.id > ? ORDER BY m.id ASC LIMIT ?
  `).all(after, bounded) : db.prepare(`
    SELECT * FROM (
      SELECT m.id, m.client_id, m.conversation_id, m.author_id, u.display_name AS author_name,
             u.short_name AS author_short, m.type, m.body, m.created_at
      FROM messages m JOIN users u ON u.id = m.author_id
      WHERE m.conversation_id = 'family' ORDER BY m.id DESC LIMIT ?
    ) ORDER BY id ASC
  `).all(bounded);
  return rows.map((row) => ({
    ...row,
    receipts: db.prepare('SELECT user_id, state, updated_at FROM receipts WHERE message_id = ?').all(row.id),
    voice: row.type === 'voice' ? db.prepare(`
      SELECT mime_type, sample_rate, sample_count, duration_ms, byte_length
      FROM voice_clips WHERE message_id = ?
    `).get(row.id) : undefined,
  }));
}

function presenceRows() {
  const now = Date.now();
  return db.prepare(`
    SELECT u.id, u.display_name, u.short_name, p.last_seen_at, p.connected
    FROM users u LEFT JOIN presence p ON p.user_id = u.id
    WHERE u.revoked_at IS NULL ORDER BY u.display_name
  `).all().map((row) => ({
    ...row,
    status: row.connected && now - row.last_seen_at < 45_000 ? 'online' :
      row.last_seen_at && now - row.last_seen_at < 15 * 60_000 ? 'recent' : 'offline',
  }));
}

function receiptRows(after = 0, limit = 100) {
  const bounded = Math.min(Math.max(limit, 1), 100);
  return db.prepare(`
    SELECT message_id, user_id, state, updated_at FROM receipts
    WHERE updated_at > ? ORDER BY updated_at ASC LIMIT ?
  `).all(after, bounded);
}

function touchPresence(user, connected = true) {
  db.prepare(`
    INSERT INTO presence(user_id, last_seen_at, connected) VALUES (?, ?, ?)
    ON CONFLICT(user_id) DO UPDATE SET last_seen_at=excluded.last_seen_at, connected=excluded.connected
  `).run(user, Date.now(), connected ? 1 : 0);
}

function publish(event, payload) {
  const packet = `event: ${event}\ndata: ${JSON.stringify(payload)}\n\n`;
  for (const res of clients) res.write(packet);
  for (const wake of longPolls) wake();
  longPolls.clear();
}

function setReceipt(messageId, user, state) {
  const rank = { server: 0, delivered: 1, read: 2 };
  const current = db.prepare('SELECT state FROM receipts WHERE message_id = ? AND user_id = ?').get(messageId, user);
  if (current && rank[current.state] >= rank[state]) return false;
  db.prepare(`
    INSERT INTO receipts(message_id, user_id, state, updated_at) VALUES (?, ?, ?, ?)
    ON CONFLICT(message_id, user_id) DO UPDATE SET state=excluded.state, updated_at=excluded.updated_at
  `).run(messageId, user, state, Date.now());
  return true;
}

function sameOrigin(req) {
  const origin = req.headers.origin;
  return !origin || origin === 'https://le954.ca' || origin === `https://${requestHost(req)}` || origin === `http://${req.headers.host}`;
}

function websocketFrame(payload, opcode = 1) {
  const content = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  const header = content.length < 126 ? Buffer.from([0x80 | opcode, content.length]) :
    Buffer.from([0x80 | opcode, 126, content.length >> 8, content.length & 0xff]);
  return Buffer.concat([header, content]);
}

function walkieBroadcast(value, opcode = 1, except = null) {
  const frame = websocketFrame(opcode === 1 ? JSON.stringify(value) : value, opcode);
  for (const socket of walkieClients.keys()) if (socket !== except && !socket.destroyed) socket.write(frame);
}

function releaseWalkie(user, reason = 'released') {
  if (walkieSpeaker !== user) return;
  walkieSpeaker = null; walkieStartedAt = 0;
  walkieBroadcast({ type: 'ptt_stop', user, reason, at: Date.now() });
}

// Release a client that acquired the channel but stopped sending before it
// could deliver another frame (lost WiFi, suspended browser, or crashed app).
setInterval(() => {
  if (walkieSpeaker && Date.now() - walkieStartedAt > 30_000) releaseWalkie(walkieSpeaker, 'timeout');
}, 1000).unref();

const server = createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  try {
    if (url.pathname === '/healthz') {
      return json(res, 200, { ok: true, service: 'supachat', time: Date.now() });
    }
    if (url.pathname === '/login' && req.method === 'GET') {
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
      return res.end(loginHtml);
    }
    if (url.pathname === '/login' && req.method === 'POST') {
      if (!sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const ip = req.socket.remoteAddress || 'unknown';
      const attempt = loginAttempts.get(ip) || { count: 0, reset: 0 };
      if (attempt.reset > Date.now() && attempt.count >= 8) return json(res, 429, { error: 'try_later' });
      const payload = await body(req);
      const valid = payload.username?.toLowerCase() === 'papa' && verifyPassword(payload.password || '', config.papaPasswordHash);
      if (!valid) {
        loginAttempts.set(ip, { count: attempt.reset > Date.now() ? attempt.count + 1 : 1, reset: Date.now() + 15 * 60_000 });
        return json(res, 401, { error: 'invalid_login' });
      }
      loginAttempts.delete(ip);
      const cookie = signSession({ user: 'papa', exp: Date.now() + 7 * 24 * 60 * 60_000 });
      return json(res, 200, { ok: true }, { 'set-cookie': `supachat_session=${encodeURIComponent(cookie)}; Path=${requestBase(req) || '/'}; HttpOnly; Secure; SameSite=Strict; Max-Age=604800` });
    }
    if (url.pathname === '/logout' && req.method === 'POST') {
      if (!sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      return json(res, 200, { ok: true }, { 'set-cookie': `supachat_session=; Path=${requestBase(req) || '/'}; HttpOnly; Secure; SameSite=Strict; Max-Age=0` });
    }
    if (url.pathname === '/api/native/session' && req.method === 'POST') {
      const value = String(req.headers.authorization || '');
      if (!value.startsWith('Bearer ')) return json(res, 401, { error: 'invalid_native_token' });
      const profile = await authentikProfile(value.slice(7));
      if (!profile) return json(res, 401, { error: 'invalid_native_token' });
      const username = String(profile.preferred_username || profile.email).toLowerCase();
      const id = authentikUserId(username, profile.sub);
      const displayName = String(profile.name || username).trim().slice(0, 80) || username.slice(0, 80);
      const shortName = displayName.split(/\s+/)[0].slice(0, 12) || 'Friend';
      db.prepare(`
        INSERT INTO users(id, display_name, short_name, kind) VALUES (?, ?, ?, 'web')
        ON CONFLICT(id) DO UPDATE SET display_name = excluded.display_name, short_name = excluded.short_name
      `).run(id, displayName, shortName);
      db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run(id);
      const token = signSession({ user: id, kind: 'native', exp: Date.now() + 30 * 24 * 60 * 60_000 });
      const user = db.prepare('SELECT id, display_name, short_name, role FROM users WHERE id = ?').get(id);
      return json(res, 200, { token, user });
    }
    if (assets.has(url.pathname)) {
      const [type, content] = assets.get(url.pathname);
      res.writeHead(200, { 'content-type': type, 'cache-control': 'public, max-age=300' });
      return res.end(content);
    }
    if (url.pathname === '/' || url.pathname === '/index.html') {
      if (!webUser(req)) {
        res.writeHead(303, { location: `${requestBase(req)}/login` });
        return res.end();
      }
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
      return res.end(appHtml);
    }

    const user = actor(req);
    if (!user) return json(res, 401, { error: 'unauthorized' });
    touchPresence(user);

    if (url.pathname === '/api/session' && req.method === 'GET') {
      const profile = db.prepare('SELECT id, display_name, short_name, role FROM users WHERE id = ?').get(user);
      return json(res, 200, { user: profile, auth: deviceUser(req) ? 'device' : nativeUser(req) ? 'native' : (String(req.headers['x-authentik-uid'] || '') ? 'authentik' : 'legacy') });
    }

    if (url.pathname === '/api/notifications/devices' && req.method === 'POST') {
      if (!nativeUser(req)) return json(res, 403, { error: 'native_session_required' });
      const payload = await body(req);
      const token = String(payload.token || '').trim();
      const provider = String(payload.provider || '');
      const platform = String(payload.platform || '').slice(0, 20);
      const deviceName = String(payload.device_name || 'Android device').trim().slice(0, 80);
      if (provider !== 'expo' || !/^Expo(nent)?PushToken\[[^\]]+\]$/.test(token) || platform !== 'android') {
        return json(res, 400, { error: 'invalid_notification_device' });
      }
      const now = Date.now();
      db.prepare(`
        INSERT INTO notification_devices(user_id, provider, token, platform, device_name, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(token) DO UPDATE SET user_id = excluded.user_id, platform = excluded.platform,
          device_name = excluded.device_name, enabled = 1, updated_at = excluded.updated_at, last_error = NULL
      `).run(user, provider, token, platform, deviceName, now, now);
      return json(res, 200, { ok: true });
    }

    if (url.pathname === '/api/messages' && req.method === 'GET') {
      const messages = messageRows(Number(url.searchParams.get('after') || 0), Number(url.searchParams.get('limit') || 100));
      if (webUser(req)) {
        for (const message of messages) {
          if (message.author_id !== user && setReceipt(message.id, user, 'read')) {
            publish('receipt', { message_id: message.id, user_id: user, state: 'read', updated_at: Date.now() });
          }
        }
      }
      return json(res, 200, { messages: messageRows(Number(url.searchParams.get('after') || 0), Number(url.searchParams.get('limit') || 100)) });
    }
    if (url.pathname === '/api/messages' && req.method === 'POST') {
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req);
      const text = typeof payload.body === 'string' ? payload.body.trim() : '';
      const clientId = String(payload.client_id || '').slice(0, 80);
      if (!text || [...text].length > 140 || !clientId) return json(res, 400, { error: 'invalid_message', max_length: 140 });
      let result;
      try {
        result = db.prepare("INSERT INTO messages(conversation_id, author_id, client_id, body, created_at) VALUES ('family', ?, ?, ?, ?)").run(user, clientId, text, Date.now());
      } catch (error) {
        if (!String(error).includes('UNIQUE')) throw error;
      }
      const row = db.prepare('SELECT id FROM messages WHERE author_id = ? AND client_id = ?').get(user, clientId);
      setReceipt(row.id, user, 'server');
      const message = messageRows(row.id - 1, 1)[0];
      if (result) { publish('message', message); sendExpoNotifications(message); }
      return json(res, result ? 201 : 200, { message });
    }
    if (url.pathname === '/api/voice' && req.method === 'POST') {
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const clientId = String(req.headers['x-client-id'] || '').slice(0, 80);
      const sampleRate = Number(req.headers['x-sample-rate'] || 8000);
      const samples = await rawBody(req);
      if (!clientId || sampleRate !== 8000 || !samples.length || samples.length > 80_000 || samples.length % 2) {
        return json(res, 400, { error: 'invalid_voice_clip', max_seconds: 5, format: 'pcm_s16le_8000_mono' });
      }
      let inserted = false;
      try {
        const result = db.prepare(`
          INSERT INTO messages(conversation_id, author_id, client_id, type, body, created_at)
          VALUES ('family', ?, ?, 'voice', '[voice]', ?)
        `).run(user, clientId, Date.now());
        const messageId = Number(result.lastInsertRowid);
        const fileName = `${messageId}-${randomBytes(8).toString('hex')}.pcm`;
        writeFileSync(join(voiceDir, fileName), samples, { mode: 0o600 });
        db.prepare(`
          INSERT INTO voice_clips(message_id, file_name, mime_type, sample_rate, sample_count, duration_ms, byte_length)
          VALUES (?, ?, 'audio/L16', ?, ?, ?, ?)
        `).run(messageId, fileName, sampleRate, samples.length / 2, Math.round(samples.length / 2 / sampleRate * 1000), samples.length);
        inserted = true;
      } catch (error) {
        if (!String(error).includes('UNIQUE')) throw error;
      }
      const row = db.prepare('SELECT id FROM messages WHERE author_id = ? AND client_id = ?').get(user, clientId);
      setReceipt(row.id, user, 'server');
      const message = messageRows(row.id - 1, 1)[0];
      if (inserted) { publish('message', message); sendExpoNotifications(message); }
      return json(res, inserted ? 201 : 200, { message });
    }
    const voiceMatch = url.pathname.match(/^\/api\/voice\/(\d+)\/audio$/);
    if (voiceMatch && req.method === 'GET') {
      const clip = db.prepare('SELECT * FROM voice_clips WHERE message_id = ?').get(Number(voiceMatch[1]));
      if (!clip) return json(res, 404, { error: 'voice_not_found' });
      const pcm = readFileSync(join(voiceDir, clip.file_name));
      const wantsWav = url.searchParams.get('format') === 'wav';
      const content = wantsWav ? pcmWav(pcm, clip.sample_rate) : pcm;
      res.writeHead(200, {
        'content-type': wantsWav ? 'audio/wav' : 'audio/L16; rate=8000; channels=1',
        'content-length': content.length,
        'cache-control': 'private, max-age=31536000, immutable',
        'x-sample-rate': clip.sample_rate,
      });
      return res.end(content);
    }
    if (url.pathname === '/api/receipts' && req.method === 'POST') {
      const payload = await body(req);
      const messageId = Number(payload.message_id);
      if (!Number.isInteger(messageId) || !['delivered', 'read'].includes(payload.state)) return json(res, 400, { error: 'invalid_receipt' });
      if (!db.prepare('SELECT 1 FROM messages WHERE id = ?').get(messageId)) return json(res, 404, { error: 'message_not_found' });
      setReceipt(messageId, user, payload.state);
      publish('receipt', { message_id: messageId, user_id: user, state: payload.state, updated_at: Date.now() });
      return json(res, 200, { ok: true });
    }
    if (url.pathname === '/api/presence' && req.method === 'GET') {
      return json(res, 200, { presence: presenceRows() });
    }
    if (url.pathname === '/api/events' && req.method === 'GET' && webUser(req)) {
      res.writeHead(200, { 'content-type': 'text/event-stream', 'cache-control': 'no-cache', connection: 'keep-alive', 'x-accel-buffering': 'no' });
      res.write(`event: ready\ndata: ${JSON.stringify({ time: Date.now() })}\n\n`);
      clients.add(res);
      req.on('close', () => clients.delete(res));
      return;
    }
    if (url.pathname === '/api/device/sync' && req.method === 'GET' && deviceUser(req)) {
      const after = Number(url.searchParams.get('after') || 0);
      const receiptsAfter = Number(url.searchParams.get('receipts_after') || 0);
      const limit = Math.min(Math.max(Number(url.searchParams.get('limit') || 100), 1), 100);
      const shouldWait = url.searchParams.get('wait') !== '0';
      let messages = messageRows(after, limit);
      let receipts = receiptRows(receiptsAfter, limit);
      if (shouldWait && !messages.length && !receipts.length) {
        await new Promise((resolve) => {
          const timer = setTimeout(() => { longPolls.delete(wake); resolve(); }, 25_000);
          const wake = () => { clearTimeout(timer); resolve(); };
          longPolls.add(wake);
          res.on('close', () => { clearTimeout(timer); longPolls.delete(wake); resolve(); });
        });
        messages = messageRows(after, limit);
        receipts = receiptRows(receiptsAfter, limit);
      }
      for (const message of messages) {
        if (message.author_id !== user && setReceipt(message.id, user, 'delivered')) {
          publish('receipt', { message_id: message.id, user_id: user, state: 'delivered', updated_at: Date.now() });
        }
      }
      return json(res, 200, { server_time: Date.now(), messages, receipts, presence: presenceRows() });
    }
    return json(res, 404, { error: 'not_found' });
  } catch (error) {
    console.error(error);
    return json(res, error.message === 'body_too_large' ? 413 : 500, { error: 'server_error' });
  }
});

server.on('upgrade', (req, socket) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  const user = url.pathname === '/walkie' ? actor(req) : null;
  const key = req.headers['sec-websocket-key'];
  const validUpgrade = String(req.headers.upgrade || '').toLowerCase() === 'websocket' && req.headers['sec-websocket-version'] === '13';
  const deviceBearer = String(req.headers.authorization || '').startsWith('Bearer ');
  if (!user || !key || !validUpgrade) { socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n'); return socket.destroy(); }
  if (!deviceBearer && !sameOrigin(req)) { socket.write('HTTP/1.1 403 Forbidden\r\n\r\n'); return socket.destroy(); }
  const accept = createHash('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  walkieClients.set(socket, user); touchPresence(user);
  socket.write(websocketFrame(JSON.stringify({ type: 'ready', user, speaker: walkieSpeaker })));
  walkieBroadcast({ type: 'walkie_presence', user, state: 'online' }, 1, socket);
  let buffered = Buffer.alloc(0);
  socket.on('data', (chunk) => {
    buffered = Buffer.concat([buffered, chunk]);
    while (buffered.length >= 2) {
      const opcode = buffered[0] & 0x0f; const masked = Boolean(buffered[1] & 0x80);
      if (!masked) return socket.destroy();
      let length = buffered[1] & 0x7f; let offset = 2;
      if (length === 126) { if (buffered.length < 4) return; length = buffered.readUInt16BE(2); offset = 4; }
      if (length === 127 || length > 16_384) return socket.destroy();
      const maskBytes = masked ? 4 : 0;
      if (buffered.length < offset + maskBytes + length) return;
      const mask = masked ? buffered.subarray(offset, offset + 4) : null; offset += maskBytes;
      const payload = Buffer.from(buffered.subarray(offset, offset + length));
      buffered = buffered.subarray(offset + length);
      if (mask) for (let index = 0; index < payload.length; index++) payload[index] ^= mask[index % 4];
      if (opcode === 8) return socket.end(websocketFrame(Buffer.alloc(0), 8));
      if (opcode === 9) { socket.write(websocketFrame(payload, 10)); continue; }
      if (opcode === 1) {
        let message; try { message = JSON.parse(payload); } catch { continue; }
        if (message.type === 'ptt_start') {
          if (!walkieSpeaker) {
            walkieSpeaker = user; walkieStartedAt = Date.now();
            walkieBroadcast({ type: 'ptt_start', user, sample_rate: 8000, at: walkieStartedAt });
          } else socket.write(websocketFrame(JSON.stringify({ type: 'busy', speaker: walkieSpeaker })));
        } else if (message.type === 'ptt_stop') releaseWalkie(user);
      } else if (opcode === 2 && walkieSpeaker === user) {
        if (Date.now() - walkieStartedAt > 30_000) releaseWalkie(user, 'timeout');
        else walkieBroadcast(payload, 2, socket);
      }
    }
  });
  const close = () => {
    if (!walkieClients.delete(socket)) return;
    releaseWalkie(user, 'disconnect');
    walkieBroadcast({ type: 'walkie_presence', user, state: 'offline' });
  };
  socket.on('close', close); socket.on('error', close);
});

function verifyPassword(password, stored) {
  const [algorithm, salt, expectedHex] = stored.split('$');
  if (algorithm !== 'scrypt' || !salt || !expectedHex) return false;
  const expected = Buffer.from(expectedHex, 'hex');
  const actual = scryptSync(password, Buffer.from(salt, 'hex'), expected.length);
  return expected.length === actual.length && timingSafeEqual(expected, actual);
}

export function hashPassword(password, salt = randomBytes(16)) {
  return `scrypt$${salt.toString('hex')}$${scryptSync(password, salt, 32).toString('hex')}`;
}

server.listen(config.port, config.host, () => console.log(`SupaChat listening on http://${config.host}:${config.port}`));
