import { createHash, createHmac, randomBytes, scryptSync, timingSafeEqual } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { DatabaseSync } from 'node:sqlite';
import { normalizeSpell, parseChallenge, resolveRound } from './duel.mjs';

const sourceRoot = dirname(fileURLToPath(import.meta.url));
const webRoot = join(sourceRoot, '..', 'web');
const config = {
  host: process.env.SUPACHAT_HOST || '127.0.0.1',
  port: Number(process.env.SUPACHAT_PORT || 8094),
  dataDir: process.env.SUPACHAT_DATA_DIR || join(sourceRoot, '..', 'data'),
  publicBase: (process.env.SUPACHAT_PUBLIC_BASE || '/supachat').replace(/\/$/, ''),
  portalHost: (process.env.SUPACHAT_PORTAL_HOST || 'supachat.net').toLowerCase(),
  papaAuthentikUsername: (process.env.SUPACHAT_PAPA_AUTHENTIK_USERNAME || 'papa').toLowerCase(),
  authentikUserinfoUrl: process.env.SUPACHAT_AUTHENTIK_USERINFO_URL || 'https://auth.supachat.net/application/o/userinfo/',
  mobileClientId: process.env.SUPACHAT_MOBILE_CLIENT_ID || 'supachat-android',
  nativeTestToken: process.env.SUPACHAT_NATIVE_TEST_TOKEN || '',
  authentikApiUrl: (process.env.SUPACHAT_AUTHENTIK_API_URL || 'https://auth.supachat.net/api/v3').replace(/\/$/, ''),
  authentikApiToken: process.env.SUPACHAT_AUTHENTIK_API_TOKEN || '',
  authentikInviteFlowId: process.env.SUPACHAT_AUTHENTIK_INVITE_FLOW_ID || '',
  authentikKbudsInviteFlowId: process.env.SUPACHAT_AUTHENTIK_KBUDS_INVITE_FLOW_ID || '',
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
  CREATE TABLE IF NOT EXISTS pending_room_memberships (
    username TEXT NOT NULL,
    conversation_id TEXT NOT NULL REFERENCES conversations(id),
    display_name TEXT NOT NULL,
    invitation_id TEXT,
    expires_at INTEGER NOT NULL,
    claimed_at INTEGER,
    PRIMARY KEY (username, conversation_id)
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
  CREATE TABLE IF NOT EXISTS message_reactions (
    message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
    user_id TEXT NOT NULL REFERENCES users(id), emoji TEXT NOT NULL,
    created_at INTEGER NOT NULL, PRIMARY KEY(message_id, user_id, emoji)
  );
  CREATE TABLE IF NOT EXISTS room_reads (
    conversation_id TEXT NOT NULL REFERENCES conversations(id), user_id TEXT NOT NULL REFERENCES users(id),
    last_read_message_id INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL,
    PRIMARY KEY(conversation_id, user_id)
  );
  CREATE TABLE IF NOT EXISTS duels (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL REFERENCES conversations(id),
    challenger_id TEXT NOT NULL REFERENCES users(id), opponent_id TEXT NOT NULL REFERENCES users(id),
    status TEXT NOT NULL CHECK(status IN ('pending','active','complete','declined','cancelled','expired')),
    challenger_score INTEGER NOT NULL DEFAULT 0, opponent_score INTEGER NOT NULL DEFAULT 0,
    challenger_protego INTEGER NOT NULL DEFAULT 0, opponent_protego INTEGER NOT NULL DEFAULT 0,
    round_number INTEGER NOT NULL DEFAULT 1, winner_id TEXT REFERENCES users(id),
    created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, expires_at INTEGER,
    completed_at INTEGER, terminal_reason TEXT
  );
  CREATE TABLE IF NOT EXISTS duel_choices (
    duel_id INTEGER NOT NULL REFERENCES duels(id) ON DELETE CASCADE, round_number INTEGER NOT NULL,
    user_id TEXT NOT NULL REFERENCES users(id), spell TEXT NOT NULL, submitted_at INTEGER NOT NULL,
    PRIMARY KEY(duel_id, round_number, user_id)
  );
  CREATE TABLE IF NOT EXISTS duel_rounds (
    duel_id INTEGER NOT NULL REFERENCES duels(id) ON DELETE CASCADE, round_number INTEGER NOT NULL,
    challenger_spell TEXT NOT NULL, opponent_spell TEXT NOT NULL, winner_id TEXT REFERENCES users(id),
    reason TEXT NOT NULL, resolved_at INTEGER NOT NULL, PRIMARY KEY(duel_id, round_number)
  );
  CREATE TABLE IF NOT EXISTS duel_acknowledgements (
    duel_id INTEGER NOT NULL REFERENCES duels(id) ON DELETE CASCADE,
    user_id TEXT NOT NULL REFERENCES users(id), acknowledged_at INTEGER NOT NULL,
    PRIMARY KEY(duel_id, user_id)
  );
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
db.prepare("INSERT OR IGNORE INTO conversations(id, name, kind) VALUES ('k-buds', 'K-BUDS', 'room')").run();
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('papa');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('albie');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('juju');
for (const userId of ['papa', 'albie', 'juju']) db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('k-buds', ?)").run(userId);

const clients = new Set();
const longPolls = new Set();
const walkieClients = new Map();
const walkieSpeakers = new Map();
const loginAttempts = new Map();
const typingByRoom = new Map();
const lastMessageAt = new Map();
const assets = new Map([
  ['/app.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'app.css'))]],
  ['/controls.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'controls.css'))]],
  ['/admin.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'admin.css'))]],
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

function authentikGroups(value) {
  return new Set(String(value || '').split(/[,|]/).map((group) => group.trim().toLowerCase()).filter(Boolean));
}

function claimPendingRoomMemberships(userId, username) {
  const now = Date.now();
  const pending = db.prepare('SELECT conversation_id FROM pending_room_memberships WHERE username = ? AND claimed_at IS NULL AND expires_at > ?').all(username, now);
  const grant = db.prepare('INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES (?, ?)');
  const claim = db.prepare('UPDATE pending_room_memberships SET claimed_at = ? WHERE username = ? AND conversation_id = ?');
  for (const row of pending) { grant.run(row.conversation_id, userId); claim.run(now, username, row.conversation_id); }
}
for (const migration of [
  'ALTER TABLE messages ADD COLUMN reply_to_id INTEGER REFERENCES messages(id)',
  'ALTER TABLE messages ADD COLUMN edited_at INTEGER',
  'ALTER TABLE messages ADD COLUMN deleted_at INTEGER',
]) try { db.exec(migration); } catch (error) { if (!String(error).includes('duplicate column')) throw error; }

function roomsFor(userId) {
  return db.prepare(`SELECT c.id, c.name, COALESCE(MAX(m.id), 0) AS latest_message_id,
    COUNT(CASE WHEN m.id > COALESCE(rr.last_read_message_id, 0) AND m.author_id <> ? THEN 1 END) AS unread_count
    FROM conversations c JOIN conversation_members cm ON cm.conversation_id = c.id
    LEFT JOIN messages m ON m.conversation_id = c.id
    LEFT JOIN room_reads rr ON rr.conversation_id = c.id AND rr.user_id = cm.user_id
    WHERE cm.user_id = ? GROUP BY c.id, c.name ORDER BY CASE c.id WHEN 'family' THEN 0 ELSE 1 END, c.name`).all(userId, userId);
}

function authorizedRoom(userId, requested) {
  const roomId = String(requested || '').trim().toLowerCase();
  if (!roomId) return null;
  return db.prepare('SELECT c.id, c.name FROM conversations c JOIN conversation_members cm ON cm.conversation_id = c.id WHERE c.id = ? AND cm.user_id = ?').get(roomId, userId);
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
    claimPendingRoomMemberships(id, normalizedUsername);
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
  const devices = db.prepare(`SELECT nd.id, nd.token FROM notification_devices nd
    JOIN conversation_members cm ON cm.user_id = nd.user_id
    WHERE nd.provider = 'expo' AND nd.enabled = 1 AND nd.user_id <> ? AND cm.conversation_id = ?`).all(message.author_id, message.conversation_id);
  if (!devices.length) return;
  const payloads = devices.map((device) => ({
    to: device.token,
    sound: 'default',
    title: `${message.author_name} · ${message.conversation_name}`,
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

function messageRows(roomId, after = 0, limit = 100) {
  const bounded = Math.min(Math.max(limit, 1), 100);
  const rows = after > 0 ? db.prepare(`
    SELECT m.id, m.client_id, m.conversation_id, m.author_id, u.display_name AS author_name,
           u.short_name AS author_short, m.type, m.body, m.created_at, m.reply_to_id, m.edited_at, m.deleted_at
    FROM messages m JOIN users u ON u.id = m.author_id
    WHERE m.conversation_id = ? AND m.id > ? ORDER BY m.id ASC LIMIT ?
  `).all(roomId, after, bounded) : db.prepare(`
    SELECT * FROM (
      SELECT m.id, m.client_id, m.conversation_id, m.author_id, u.display_name AS author_name,
             u.short_name AS author_short, m.type, m.body, m.created_at, m.reply_to_id, m.edited_at, m.deleted_at
      FROM messages m JOIN users u ON u.id = m.author_id
      WHERE m.conversation_id = ? ORDER BY m.id DESC LIMIT ?
    ) ORDER BY id ASC
  `).all(roomId, bounded);
  return rows.map((row) => ({
    ...row, body: row.deleted_at ? '' : row.body,
    conversation_name: db.prepare('SELECT name FROM conversations WHERE id = ?').get(row.conversation_id)?.name,
    receipts: db.prepare('SELECT user_id, state, updated_at FROM receipts WHERE message_id = ?').all(row.id),
    reactions: db.prepare(`SELECT mr.emoji, COUNT(*) AS count,
      GROUP_CONCAT(u.display_name, ', ') AS names FROM message_reactions mr JOIN users u ON u.id=mr.user_id
      WHERE mr.message_id=? GROUP BY mr.emoji ORDER BY mr.emoji`).all(row.id),
    reply_to: row.reply_to_id ? db.prepare(`SELECT m.id, m.author_id, u.display_name AS author_name,
      CASE WHEN m.deleted_at IS NULL THEN m.body ELSE '' END AS body, m.deleted_at
      FROM messages m JOIN users u ON u.id=m.author_id WHERE m.id=? AND m.conversation_id=?`).get(row.reply_to_id, row.conversation_id) : null,
    voice: row.type === 'voice' ? db.prepare(`
      SELECT mime_type, sample_rate, sample_count, duration_ms, byte_length
      FROM voice_clips WHERE message_id = ?
    `).get(row.id) : undefined,
  }));
}

function presenceRows(roomId) {
  const now = Date.now();
  return db.prepare(`
    SELECT u.id, u.display_name, u.short_name, p.last_seen_at, p.connected
    FROM users u JOIN conversation_members cm ON cm.user_id = u.id LEFT JOIN presence p ON p.user_id = u.id
    WHERE u.revoked_at IS NULL AND cm.conversation_id = ? ORDER BY u.display_name
  `).all(roomId).map((row) => ({
    ...row,
    status: row.connected && now - row.last_seen_at < 45_000 ? 'online' :
      row.last_seen_at && now - row.last_seen_at < 15 * 60_000 ? 'recent' : 'offline',
  }));
}

function receiptRows(roomId, after = 0, limit = 100) {
  const bounded = Math.min(Math.max(limit, 1), 100);
  return db.prepare(`
    SELECT r.message_id, r.user_id, r.state, r.updated_at FROM receipts r JOIN messages m ON m.id = r.message_id
    WHERE m.conversation_id = ? AND r.updated_at > ? ORDER BY r.updated_at ASC LIMIT ?
  `).all(roomId, after, bounded);
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

function activeTyping(roomId, exceptUser) {
  const now=Date.now(); const result=[];
  for(const [key,value] of typingByRoom){if(value.expires_at<=now){typingByRoom.delete(key);continue;}if(key.startsWith(`${roomId}:`)&&value.user_id!==exceptUser)result.push(value);}
  return result;
}

function markRoomRead(roomId,user,messageId){
  db.prepare(`INSERT INTO room_reads(conversation_id,user_id,last_read_message_id,updated_at) VALUES (?,?,?,?)
    ON CONFLICT(conversation_id,user_id) DO UPDATE SET last_read_message_id=MAX(last_read_message_id,excluded.last_read_message_id),updated_at=excluded.updated_at`).run(roomId,user,messageId,Date.now());
}

const duelChallengeLifetimeMs = 24 * 60 * 60 * 1000;

function expirePendingDuels(roomId) {
  const now = Date.now();
  const result = db.prepare(`UPDATE duels SET status='expired',completed_at=?,terminal_reason='challenge_expired',updated_at=?
    WHERE conversation_id=? AND status='pending' AND expires_at IS NOT NULL AND expires_at<=?`).run(now,now,roomId,now);
  if (result.changes) publish('duel',{conversation_id:roomId});
}

function activeDuelRowFor(user, roomId) {
  expirePendingDuels(roomId);
  return db.prepare("SELECT * FROM duels WHERE conversation_id=? AND status IN ('pending','active') AND (challenger_id=? OR opponent_id=?) ORDER BY id DESC LIMIT 1").get(roomId, user, user);
}

function visibleDuelRowFor(user, roomId) {
  const active = activeDuelRowFor(user, roomId);
  if (active) return active;
  return db.prepare(`SELECT d.* FROM duels d LEFT JOIN duel_acknowledgements a ON a.duel_id=d.id AND a.user_id=?
    WHERE d.conversation_id=? AND d.status IN ('complete','declined','cancelled','expired')
      AND (d.challenger_id=? OR d.opponent_id=?) AND a.duel_id IS NULL
    ORDER BY COALESCE(d.completed_at,d.updated_at) DESC,d.id DESC LIMIT 1`).get(user,roomId,user,user);
}

function duelState(user, roomId, duelId = null) {
  const duel = duelId ? db.prepare('SELECT * FROM duels WHERE id=? AND conversation_id=? AND (challenger_id=? OR opponent_id=?)').get(duelId, roomId, user, user) : visibleDuelRowFor(user, roomId);
  if (!duel) return null;
  const challenger = db.prepare('SELECT id, display_name, short_name FROM users WHERE id=?').get(duel.challenger_id);
  const opponent = db.prepare('SELECT id, display_name, short_name FROM users WHERE id=?').get(duel.opponent_id);
  const ownChoice = db.prepare('SELECT 1 FROM duel_choices WHERE duel_id=? AND round_number=? AND user_id=?').get(duel.id, duel.round_number, user);
  const lastRound = db.prepare('SELECT * FROM duel_rounds WHERE duel_id=? ORDER BY round_number DESC LIMIT 1').get(duel.id);
  const challengedByMe=duel.challenger_id===user;
  return {...duel, challenger, opponent, challenged_by_me:challengedByMe, my_choice_locked:Boolean(ownChoice), last_round:lastRound||null,
    can_accept:duel.status==='pending'&&!challengedByMe, can_decline:duel.status==='pending'&&!challengedByMe,
    can_cancel:duel.status==='pending'&&challengedByMe, can_choose:duel.status==='active'&&!ownChoice};
}

function challengeDuel(user, roomId, opponentName) {
  expirePendingDuels(roomId);
  const wanted = String(opponentName || '').trim().toLowerCase();
  const opponent = db.prepare(`SELECT u.id FROM users u JOIN conversation_members cm ON cm.user_id=u.id
    WHERE cm.conversation_id=? AND u.revoked_at IS NULL AND u.id<>? AND
    (lower(u.id)=? OR lower(u.display_name)=? OR lower(u.short_name)=?) LIMIT 1`).get(roomId, user, wanted, wanted, wanted);
  if (!opponent) return {error:'duelist_not_found'};
  const mine = activeDuelRowFor(user, roomId); const theirs = activeDuelRowFor(opponent.id, roomId);
  if (mine && mine.id !== theirs?.id) return {error:'already_dueling'};
  if (theirs?.status === 'pending' && theirs.challenger_id === opponent.id && theirs.opponent_id === user) {
    db.prepare("UPDATE duels SET status='active',updated_at=? WHERE id=?").run(Date.now(), theirs.id);
    publish('duel', {conversation_id:roomId,duel_id:theirs.id}); return {duel:duelState(user, roomId, theirs.id)};
  }
  if (mine) return {duel:duelState(user, roomId, mine.id)};
  const now=Date.now(); const result=db.prepare("INSERT INTO duels(conversation_id,challenger_id,opponent_id,status,created_at,updated_at,expires_at) VALUES (?,?,?,'pending',?,?,?)").run(roomId,user,opponent.id,now,now,now+duelChallengeLifetimeMs);
  publish('duel',{conversation_id:roomId,duel_id:Number(result.lastInsertRowid)}); return {duel:duelState(user,roomId,Number(result.lastInsertRowid))};
}

function chooseDuelSpell(user, roomId, duelId, rawSpell) {
  const spell=normalizeSpell(rawSpell); const duel=db.prepare("SELECT * FROM duels WHERE id=? AND conversation_id=? AND status='active' AND (challenger_id=? OR opponent_id=?)").get(duelId,roomId,user,user);
  if(!duel||!spell)return {error:!duel?'duel_not_active':'invalid_spell'};
  try{db.prepare('INSERT INTO duel_choices(duel_id,round_number,user_id,spell,submitted_at) VALUES (?,?,?,?,?)').run(duel.id,duel.round_number,user,spell,Date.now());}
  catch(error){if(!String(error).includes('UNIQUE'))throw error;return {error:'choice_locked'};}
  const choices=db.prepare('SELECT user_id,spell FROM duel_choices WHERE duel_id=? AND round_number=?').all(duel.id,duel.round_number);
  if(choices.length===2){
    const first=choices.find(choice=>choice.user_id===duel.challenger_id).spell; const second=choices.find(choice=>choice.user_id===duel.opponent_id).spell;
    const round=resolveRound(first,second,duel.challenger_protego,duel.opponent_protego); const winnerId=round.winner==='first'?duel.challenger_id:round.winner==='second'?duel.opponent_id:null;
    const firstScore=duel.challenger_score+(winnerId===duel.challenger_id?1:0); const secondScore=duel.opponent_score+(winnerId===duel.opponent_id?1:0); const complete=firstScore>=2||secondScore>=2;
    db.prepare('INSERT INTO duel_rounds VALUES (?,?,?,?,?,?,?)').run(duel.id,duel.round_number,first,second,winnerId,round.reason,Date.now());
    const updatedAt=Date.now();
    db.prepare("UPDATE duels SET status=?,challenger_score=?,opponent_score=?,challenger_protego=?,opponent_protego=?,round_number=round_number+1,winner_id=?,completed_at=?,terminal_reason=?,updated_at=? WHERE id=?").run(complete?'complete':'active',firstScore,secondScore,round.firstProtegoStreak,round.secondProtegoStreak,complete?winnerId:null,complete?updatedAt:null,complete?'score_limit':null,updatedAt,duel.id);
  }
  publish('duel',{conversation_id:roomId,duel_id:duel.id}); return {duel:duelState(user,roomId,duel.id),resolved:choices.length===2};
}

function finishPendingDuel(user,roomId,duelId,action){
  const duel=db.prepare("SELECT * FROM duels WHERE id=? AND conversation_id=? AND status='pending' AND (challenger_id=? OR opponent_id=?)").get(duelId,roomId,user,user);
  if(!duel)return {error:'duel_not_pending'};
  if(action==='cancel'&&duel.challenger_id!==user)return {error:'only_challenger_can_cancel'};
  if(action==='decline'&&duel.opponent_id!==user)return {error:'only_opponent_can_decline'};
  const now=Date.now(); const status=action==='cancel'?'cancelled':'declined';
  db.prepare('UPDATE duels SET status=?,completed_at=?,terminal_reason=?,updated_at=? WHERE id=?').run(status,now,status,now,duel.id);
  publish('duel',{conversation_id:roomId,duel_id:duel.id}); return {duel:duelState(user,roomId,duel.id)};
}

function acceptPendingDuel(user,roomId,duelId){
  const duel=db.prepare("SELECT * FROM duels WHERE id=? AND conversation_id=? AND status='pending' AND opponent_id=?").get(duelId,roomId,user);
  if(!duel)return {error:'duel_not_acceptible'};
  const now=Date.now(); db.prepare("UPDATE duels SET status='active',updated_at=? WHERE id=?").run(now,duel.id);
  publish('duel',{conversation_id:roomId,duel_id:duel.id}); return {duel:duelState(user,roomId,duel.id)};
}

function acknowledgeDuel(user,roomId,duelId){
  const duel=db.prepare("SELECT id FROM duels WHERE id=? AND conversation_id=? AND status IN ('complete','declined','cancelled','expired') AND (challenger_id=? OR opponent_id=?)").get(duelId,roomId,user,user);
  if(!duel)return {error:'duel_not_terminal'};
  db.prepare('INSERT OR IGNORE INTO duel_acknowledgements(duel_id,user_id,acknowledged_at) VALUES (?,?,?)').run(duel.id,user,Date.now());
  return {ok:true,duel:duelState(user,roomId)};
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
  return !origin || origin === `https://${requestHost(req)}` || origin === `http://${req.headers.host}`;
}

function websocketFrame(payload, opcode = 1) {
  const content = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  const header = content.length < 126 ? Buffer.from([0x80 | opcode, content.length]) :
    Buffer.from([0x80 | opcode, 126, content.length >> 8, content.length & 0xff]);
  return Buffer.concat([header, content]);
}

function walkieBroadcast(roomId, value, opcode = 1, except = null) {
  const frame = websocketFrame(opcode === 1 ? JSON.stringify(value) : value, opcode);
  for (const [socket, client] of walkieClients) if (client.roomId === roomId && socket !== except && !socket.destroyed) socket.write(frame);
}

function releaseWalkie(roomId, user, reason = 'released') {
  if (walkieSpeakers.get(roomId)?.user !== user) return;
  walkieSpeakers.delete(roomId);
  walkieBroadcast(roomId, { type: 'ptt_stop', user, reason, at: Date.now() });
}

// Release a client that acquired the channel but stopped sending before it
// could deliver another frame (lost WiFi, suspended browser, or crashed app).
setInterval(() => {
  for (const [roomId, speaker] of walkieSpeakers) if (Date.now() - speaker.startedAt > 30_000) releaseWalkie(roomId, speaker.user, 'timeout');
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
      claimPendingRoomMemberships(id, username);
      const token = signSession({ user: id, kind: 'native', exp: Date.now() + 30 * 24 * 60 * 60_000 });
      const user = db.prepare('SELECT id, display_name, short_name, role FROM users WHERE id = ?').get(id);
      return json(res, 200, { token, user, rooms: roomsFor(id) });
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
      return json(res, 200, { user: profile, rooms: roomsFor(user), auth: deviceUser(req) ? 'device' : nativeUser(req) ? 'native' : (String(req.headers['x-authentik-uid'] || '') ? 'authentik' : 'legacy') });
    }

    if (url.pathname === '/api/rooms' && req.method === 'GET') return json(res, 200, { rooms: roomsFor(user) });

    if (url.pathname === '/api/duels/current' && req.method === 'GET') {
      const room=authorizedRoom(user,url.searchParams.get('room')); if(!room)return json(res,url.searchParams.get('room')?403:400,{error:url.searchParams.get('room')?'room_forbidden':'room_required'});
      return json(res,200,{duel:duelState(user,room.id)});
    }
    if (url.pathname === '/api/duels/challenge' && req.method === 'POST') {
      if(webUser(req)&&!sameOrigin(req))return json(res,403,{error:'origin_rejected'});
      const payload=await body(req); const room=authorizedRoom(user,payload.room_id); if(!room)return json(res,payload.room_id?403:400,{error:payload.room_id?'room_forbidden':'room_required'});
      const result=challengeDuel(user,room.id,payload.opponent); return json(res,result.error?(result.error==='duelist_not_found'?404:409):200,result);
    }
    const duelChoicePath=url.pathname.match(/^\/api\/duels\/(\d+)\/choice$/);
    if(duelChoicePath&&req.method==='POST'){
      if(webUser(req)&&!sameOrigin(req))return json(res,403,{error:'origin_rejected'});
      const payload=await body(req); const room=authorizedRoom(user,payload.room_id); if(!room)return json(res,payload.room_id?403:400,{error:payload.room_id?'room_forbidden':'room_required'});
      const result=chooseDuelSpell(user,room.id,Number(duelChoicePath[1]),payload.spell); return json(res,result.error?409:200,result);
    }
    const duelActionPath=url.pathname.match(/^\/api\/duels\/(\d+)\/(accept|decline|cancel|acknowledge)$/);
    if(duelActionPath&&req.method==='POST'){
      if(webUser(req)&&!sameOrigin(req))return json(res,403,{error:'origin_rejected'});
      const payload=await body(req); const room=authorizedRoom(user,payload.room_id); if(!room)return json(res,payload.room_id?403:400,{error:payload.room_id?'room_forbidden':'room_required'});
      const action=duelActionPath[2]; const result=action==='acknowledge'?acknowledgeDuel(user,room.id,Number(duelActionPath[1])):action==='accept'?acceptPendingDuel(user,room.id,Number(duelActionPath[1])):finishPendingDuel(user,room.id,Number(duelActionPath[1]),action);
      return json(res,result.error?409:200,result);
    }

    if(url.pathname==='/api/typing'&&req.method==='POST'){
      if(webUser(req)&&!sameOrigin(req))return json(res,403,{error:'origin_rejected'});
      const payload=await body(req); const room=authorizedRoom(user,payload.room_id); if(!room)return json(res,payload.room_id?403:400,{error:payload.room_id?'room_forbidden':'room_required'});
      const key=`${room.id}:${user}`; if(payload.typing)typingByRoom.set(key,{user_id:user,display_name:db.prepare('SELECT display_name FROM users WHERE id=?').get(user).display_name,expires_at:Date.now()+5000});else typingByRoom.delete(key);
      publish('typing',{conversation_id:room.id,user_id:user,typing:Boolean(payload.typing)}); return json(res,200,{ok:true});
    }

    if(url.pathname==='/api/reads'&&req.method==='POST'){
      if(webUser(req)&&!sameOrigin(req))return json(res,403,{error:'origin_rejected'});
      const payload=await body(req); const room=authorizedRoom(user,payload.room_id); if(!room)return json(res,payload.room_id?403:400,{error:payload.room_id?'room_forbidden':'room_required'});
      const lastId=Number(payload.message_id||0); const valid=lastId===0||db.prepare('SELECT 1 FROM messages WHERE id=? AND conversation_id=?').get(lastId,room.id); if(!valid)return json(res,400,{error:'invalid_read_marker'});
      db.prepare(`INSERT INTO room_reads(conversation_id,user_id,last_read_message_id,updated_at) VALUES (?,?,?,?)
        ON CONFLICT(conversation_id,user_id) DO UPDATE SET last_read_message_id=MAX(last_read_message_id,excluded.last_read_message_id),updated_at=excluded.updated_at`).run(room.id,user,lastId,Date.now());
      return json(res,200,{ok:true,rooms:roomsFor(user)});
    }

    const admin = db.prepare('SELECT role FROM users WHERE id = ?').get(user)?.role === 'admin';
    if (url.pathname === '/api/admin/groups' && req.method === 'GET') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      const groups = db.prepare(`SELECT c.id, c.name, COUNT(cm.user_id) AS member_count FROM conversations c
        LEFT JOIN conversation_members cm ON cm.conversation_id = c.id WHERE c.kind IN ('shared','room') GROUP BY c.id ORDER BY c.name`).all();
      const members = db.prepare(`SELECT cm.conversation_id, u.id, u.display_name, u.short_name, u.kind, u.role
        FROM conversation_members cm JOIN users u ON u.id = cm.user_id ORDER BY u.display_name`).all();
      const users = db.prepare('SELECT id, display_name, short_name, kind, role FROM users WHERE revoked_at IS NULL ORDER BY display_name').all();
      return json(res, 200, { groups: groups.map((group) => ({ ...group, members: members.filter((member) => member.conversation_id === group.id) })), users });
    }
    if (url.pathname === '/api/admin/groups' && req.method === 'POST') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req); const name = String(payload.name || '').trim();
      const id = String(payload.id || name).trim().toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 40);
      if (!/^[a-z0-9][a-z0-9-]{1,39}$/.test(id) || name.length < 2 || name.length > 60) return json(res, 400, { error: 'invalid_group' });
      try { db.prepare("INSERT INTO conversations(id, name, kind) VALUES (?, ?, 'room')").run(id, name); }
      catch (error) { if (String(error).includes('UNIQUE')) return json(res, 409, { error: 'group_exists' }); throw error; }
      db.prepare('INSERT INTO conversation_members(conversation_id, user_id) VALUES (?, ?)').run(id, user);
      return json(res, 201, { group: { id, name, members: [db.prepare('SELECT id, display_name, short_name, kind, role FROM users WHERE id = ?').get(user)] } });
    }
    const memberPath = url.pathname.match(/^\/api\/admin\/groups\/([^/]+)\/members(?:\/([^/]+))?$/);
    if (memberPath && ['POST','DELETE'].includes(req.method)) {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const roomId = decodeURIComponent(memberPath[1]);
      if (!db.prepare("SELECT 1 FROM conversations WHERE id = ? AND kind IN ('shared','room')").get(roomId)) return json(res, 404, { error: 'group_not_found' });
      if (req.method === 'POST') {
        const payload = await body(req); const memberId = String(payload.user_id || '');
        if (!db.prepare('SELECT 1 FROM users WHERE id = ? AND revoked_at IS NULL').get(memberId)) return json(res, 404, { error: 'user_not_found' });
        db.prepare('INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES (?, ?)').run(roomId, memberId);
        return json(res, 200, { ok: true });
      }
      const memberId = decodeURIComponent(memberPath[2] || '');
      if (memberId === user) return json(res, 409, { error: 'cannot_remove_self' });
      db.prepare('DELETE FROM conversation_members WHERE conversation_id = ? AND user_id = ?').run(roomId, memberId);
      return json(res, 200, { ok: true });
    }

    if (url.pathname === '/api/admin/invitations' && req.method === 'POST') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req);
      const username = String(payload.username || '').trim().toLowerCase();
      const displayName = String(payload.display_name || '').trim();
      const email = String(payload.email || '').trim().toLowerCase();
      const roomId = String(payload.room_id || '').trim().toLowerCase();
      if (!db.prepare("SELECT 1 FROM conversations WHERE id = ? AND kind IN ('shared','room')").get(roomId) || !/^[a-z0-9][a-z0-9._-]{1,31}$/.test(username) || !displayName || displayName.length > 80 || (email && !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email))) {
        return json(res, 400, { error: 'invalid_invitation' });
      }
      const expiresAt = Date.now() + 7 * 24 * 60 * 60_000;
      let invitation;
      if (config.nativeTestToken) invitation = { pk: '00000000-0000-4000-8000-000000000001' };
      else {
        const inviteFlowId = config.authentikInviteFlowId;
        if (!config.authentikApiToken || !inviteFlowId) return json(res, 503, { error: 'invites_not_configured' });
        const invitationResponse = await fetch(`${config.authentikApiUrl}/stages/invitation/invitations/`, {
          method: 'POST', headers: { authorization: `Bearer ${config.authentikApiToken}`, 'content-type': 'application/json', accept: 'application/json' },
          body: JSON.stringify({ name: `supachat_${roomId}_${Date.now()}`, expires: new Date(expiresAt).toISOString(), fixed_data: { username, name: displayName, email }, single_use: true, flow: inviteFlowId }),
        });
        if (!invitationResponse.ok) {
          console.error('Authentik invitation creation failed', invitationResponse.status, await invitationResponse.text());
          return json(res, 502, { error: 'identity_provider_failed' });
        }
        invitation = await invitationResponse.json();
      }
      db.prepare(`INSERT INTO pending_room_memberships(username, conversation_id, display_name, invitation_id, expires_at, claimed_at)
        VALUES (?, ?, ?, ?, ?, NULL) ON CONFLICT(username, conversation_id) DO UPDATE SET display_name=excluded.display_name,
        invitation_id=excluded.invitation_id, expires_at=excluded.expires_at, claimed_at=NULL`).run(username, roomId, displayName, invitation.pk, expiresAt);
      const enrollmentSlug = 'supachat-invitation-enrollment';
      const enrollmentUrl = new URL(`https://auth.${config.portalHost}/if/flow/${enrollmentSlug}/`);
      enrollmentUrl.searchParams.set('itoken', invitation.pk);
      enrollmentUrl.searchParams.set('next', `https://${config.portalHost}/?welcome=1`);
      enrollmentUrl.searchParams.set('room', roomId);
      return json(res, 201, { url: enrollmentUrl.toString(), username, room_id: roomId, expires_at: expiresAt });
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

    const reactionPath=url.pathname.match(/^\/api\/messages\/(\d+)\/reactions$/);
    if(reactionPath&&req.method==='POST'){
      if(webUser(req)&&!sameOrigin(req))return json(res,403,{error:'origin_rejected'});
      const payload=await body(req); const messageId=Number(reactionPath[1]); const emoji=String(payload.emoji||'');
      const message=db.prepare(`SELECT m.id,m.conversation_id FROM messages m JOIN conversation_members cm ON cm.conversation_id=m.conversation_id WHERE m.id=? AND cm.user_id=?`).get(messageId,user);
      if(!message)return json(res,404,{error:'message_not_found'}); if(!['👍','❤️','😂','😮','😢','🎉'].includes(emoji))return json(res,400,{error:'invalid_reaction'});
      const existing=db.prepare('SELECT 1 FROM message_reactions WHERE message_id=? AND user_id=? AND emoji=?').get(messageId,user,emoji);
      if(existing)db.prepare('DELETE FROM message_reactions WHERE message_id=? AND user_id=? AND emoji=?').run(messageId,user,emoji);
      else db.prepare('INSERT INTO message_reactions VALUES (?,?,?,?)').run(messageId,user,emoji,Date.now());
      const updated=messageRows(message.conversation_id,messageId-1,1)[0]; publish('message_update',updated); return json(res,200,{message:updated});
    }
    const messagePath=url.pathname.match(/^\/api\/messages\/(\d+)$/);
    if(messagePath&&['PATCH','DELETE'].includes(req.method)){
      if(webUser(req)&&!sameOrigin(req))return json(res,403,{error:'origin_rejected'});
      const messageId=Number(messagePath[1]); const existing=db.prepare('SELECT * FROM messages WHERE id=?').get(messageId);
      if(!existing||existing.author_id!==user)return json(res,404,{error:'message_not_found'}); if(existing.type!=='text')return json(res,409,{error:'message_not_editable'});
      if(req.method==='PATCH'){
        const payload=await body(req); const text=String(payload.body||'').trim(); if(!text||[...text].length>140)return json(res,400,{error:'invalid_message',max_length:140});
        db.prepare('UPDATE messages SET body=?,edited_at=? WHERE id=?').run(text,Date.now(),messageId);
      }else db.prepare("UPDATE messages SET body='',deleted_at=? WHERE id=?").run(Date.now(),messageId);
      const updated=messageRows(existing.conversation_id,messageId-1,1)[0]; publish('message_update',updated); return json(res,200,{message:updated});
    }

    if (url.pathname === '/api/messages' && req.method === 'GET') {
      if (!url.searchParams.get('room')) return json(res, 400, { error: 'room_required' });
      const room = authorizedRoom(user, url.searchParams.get('room'));
      if (!room) return json(res, 403, { error: 'room_forbidden' });
      const messages = messageRows(room.id, Number(url.searchParams.get('after') || 0), Number(url.searchParams.get('limit') || 100));
      if (webUser(req)) {
        for (const message of messages) {
          if (message.author_id !== user && setReceipt(message.id, user, 'read')) {
            publish('receipt', { message_id: message.id, user_id: user, state: 'read', updated_at: Date.now() });
          }
        }
        markRoomRead(room.id,user,Math.max(0,...messages.map(message=>message.id)));
      }
      return json(res, 200, { room, messages: messageRows(room.id, Number(url.searchParams.get('after') || 0), Number(url.searchParams.get('limit') || 100)) });
    }
    if (url.pathname === '/api/messages' && req.method === 'POST') {
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req);
      if (!payload.room_id) return json(res, 400, { error: 'room_required' });
      const room = authorizedRoom(user, payload.room_id);
      if (!room) return json(res, 403, { error: 'room_forbidden' });
      const text = typeof payload.body === 'string' ? payload.body.trim() : '';
      const clientId = String(payload.client_id || '').slice(0, 80);
      const replyToId = payload.reply_to_id == null ? null : Number(payload.reply_to_id);
      if (!text || [...text].length > 140 || !clientId) return json(res, 400, { error: 'invalid_message', max_length: 140 });
      if(replyToId&&!db.prepare('SELECT 1 FROM messages WHERE id=? AND conversation_id=?').get(replyToId,room.id))return json(res,400,{error:'invalid_reply'});
      const existing = db.prepare('SELECT id, conversation_id, body, type, reply_to_id FROM messages WHERE author_id = ? AND client_id = ?').get(user, clientId);
      if (existing && (existing.conversation_id !== room.id || existing.body !== text || existing.type !== 'text' || existing.reply_to_id !== replyToId)) {
        return json(res, 409, { error: 'client_id_conflict' });
      }
      const now=Date.now(); const retryAfter=Math.max(0,1000-(now-(lastMessageAt.get(user)||0)));
      if(!existing&&retryAfter>0)return json(res,429,{error:'message_cooldown',retry_after_ms:retryAfter},{'retry-after':String(Math.ceil(retryAfter/1000))});
      let result;
      try {
        result = db.prepare('INSERT INTO messages(conversation_id, author_id, client_id, body, created_at, reply_to_id) VALUES (?, ?, ?, ?, ?, ?)').run(room.id, user, clientId, text, now, replyToId);
      } catch (error) {
        if (!String(error).includes('UNIQUE')) throw error;
      }
      const row = db.prepare('SELECT id FROM messages WHERE author_id = ? AND client_id = ?').get(user, clientId);
      setReceipt(row.id, user, 'server');
      const message = messageRows(room.id, row.id - 1, 1)[0];
      let duel;
      if (result) { lastMessageAt.set(user,now); const opponent=parseChallenge(text); if(opponent)duel=challengeDuel(user,room.id,opponent).duel; publish('message', message); sendExpoNotifications(message); }
      return json(res, result ? 201 : 200, { message, duel });
    }
    if (url.pathname === '/api/voice' && req.method === 'POST') {
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const clientId = String(req.headers['x-client-id'] || '').slice(0, 80);
      const sampleRate = Number(req.headers['x-sample-rate'] || 8000);
      const samples = await rawBody(req);
      if (!req.headers['x-room-id']) return json(res, 400, { error: 'room_required' });
      const room = authorizedRoom(user, req.headers['x-room-id']);
      if (!room) return json(res, 403, { error: 'room_forbidden' });
      if (!clientId || sampleRate !== 8000 || !samples.length || samples.length > 80_000 || samples.length % 2) {
        return json(res, 400, { error: 'invalid_voice_clip', max_seconds: 5, format: 'pcm_s16le_8000_mono' });
      }
      const existing = db.prepare('SELECT id, conversation_id, type FROM messages WHERE author_id = ? AND client_id = ?').get(user, clientId);
      if (existing && (existing.conversation_id !== room.id || existing.type !== 'voice')) {
        return json(res, 409, { error: 'client_id_conflict' });
      }
      const now=Date.now(); const retryAfter=Math.max(0,1000-(now-(lastMessageAt.get(user)||0)));
      if(!existing&&retryAfter>0)return json(res,429,{error:'message_cooldown',retry_after_ms:retryAfter},{'retry-after':String(Math.ceil(retryAfter/1000))});
      let inserted = false;
      try {
        const result = db.prepare(`
          INSERT INTO messages(conversation_id, author_id, client_id, type, body, created_at)
          VALUES (?, ?, ?, 'voice', '[voice]', ?)
        `).run(room.id, user, clientId, now);
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
      const message = messageRows(room.id, row.id - 1, 1)[0];
      if (inserted) { lastMessageAt.set(user,now); publish('message', message); sendExpoNotifications(message); }
      return json(res, inserted ? 201 : 200, { message });
    }
    const voiceMatch = url.pathname.match(/^\/api\/voice\/(\d+)\/audio$/);
    if (voiceMatch && req.method === 'GET') {
      const clip = db.prepare(`SELECT vc.* FROM voice_clips vc JOIN messages m ON m.id = vc.message_id
        JOIN conversation_members cm ON cm.conversation_id = m.conversation_id
        WHERE vc.message_id = ? AND cm.user_id = ?`).get(Number(voiceMatch[1]), user);
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
      const receiptMessage=db.prepare('SELECT m.conversation_id FROM messages m JOIN conversation_members cm ON cm.conversation_id=m.conversation_id WHERE m.id = ? AND cm.user_id = ?').get(messageId, user);
      if (!receiptMessage) return json(res, 404, { error: 'message_not_found' });
      setReceipt(messageId, user, payload.state);
      if(payload.state==='read')markRoomRead(receiptMessage.conversation_id,user,messageId);
      publish('receipt', { message_id: messageId, user_id: user, state: payload.state, updated_at: Date.now() });
      return json(res, 200, { ok: true });
    }
    if (url.pathname === '/api/presence' && req.method === 'GET') {
      if (!url.searchParams.get('room')) return json(res, 400, { error: 'room_required' });
      const room = authorizedRoom(user, url.searchParams.get('room'));
      if (!room) return json(res, 403, { error: 'room_forbidden' });
      return json(res, 200, { room, presence: presenceRows(room.id) });
    }
    if (url.pathname === '/api/events' && req.method === 'GET' && webUser(req)) {
      res.writeHead(200, { 'content-type': 'text/event-stream', 'cache-control': 'no-cache', connection: 'keep-alive', 'x-accel-buffering': 'no' });
      res.write(`event: ready\ndata: ${JSON.stringify({ time: Date.now() })}\n\n`);
      clients.add(res);
      req.on('close', () => clients.delete(res));
      return;
    }
    if (url.pathname === '/api/device/sync' && req.method === 'GET' && deviceUser(req)) {
      if (!url.searchParams.get('room')) return json(res, 400, { error: 'room_required' });
      const room = authorizedRoom(user, url.searchParams.get('room'));
      if (!room) return json(res, 403, { error: 'room_forbidden' });
      const after = Number(url.searchParams.get('after') || 0);
      const receiptsAfter = Number(url.searchParams.get('receipts_after') || 0);
      const limit = Math.min(Math.max(Number(url.searchParams.get('limit') || 100), 1), 100);
      const shouldWait = url.searchParams.get('wait') !== '0';
      let messages = messageRows(room.id, after, limit);
      let receipts = receiptRows(room.id, receiptsAfter, limit);
      if (shouldWait && !messages.length && !receipts.length) {
        await new Promise((resolve) => {
          const timer = setTimeout(() => { longPolls.delete(wake); resolve(); }, 25_000);
          const wake = () => { clearTimeout(timer); resolve(); };
          longPolls.add(wake);
          res.on('close', () => { clearTimeout(timer); longPolls.delete(wake); resolve(); });
        });
        messages = messageRows(room.id, after, limit);
        receipts = receiptRows(room.id, receiptsAfter, limit);
      }
      for (const message of messages) {
        if (message.author_id !== user && setReceipt(message.id, user, 'delivered')) {
          publish('receipt', { message_id: message.id, user_id: user, state: 'delivered', updated_at: Date.now() });
        }
      }
      return json(res, 200, { server_time: Date.now(), room, rooms: roomsFor(user), messages, receipts, presence: presenceRows(room.id), typing:activeTyping(room.id,user), duel:duelState(user,room.id) });
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
  const room = user ? authorizedRoom(user, url.searchParams.get('room')) : null;
  const key = req.headers['sec-websocket-key'];
  const validUpgrade = String(req.headers.upgrade || '').toLowerCase() === 'websocket' && req.headers['sec-websocket-version'] === '13';
  const deviceBearer = String(req.headers.authorization || '').startsWith('Bearer ');
  if (!user || !room || !key || !validUpgrade) { socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n'); return socket.destroy(); }
  if (!deviceBearer && !sameOrigin(req)) { socket.write('HTTP/1.1 403 Forbidden\r\n\r\n'); return socket.destroy(); }
  const accept = createHash('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  walkieClients.set(socket, { user, roomId: room.id }); touchPresence(user);
  socket.write(websocketFrame(JSON.stringify({ type: 'ready', user, speaker: walkieSpeakers.get(room.id)?.user || null })));
  walkieBroadcast(room.id, { type: 'walkie_presence', user, state: 'online' }, 1, socket);
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
          const speaker = walkieSpeakers.get(room.id);
          if (!speaker) {
            const startedAt = Date.now(); walkieSpeakers.set(room.id, { user, startedAt });
            walkieBroadcast(room.id, { type: 'ptt_start', user, sample_rate: 8000, at: startedAt });
          } else socket.write(websocketFrame(JSON.stringify({ type: 'busy', speaker: speaker.user })));
        } else if (message.type === 'ptt_stop') releaseWalkie(room.id, user);
      } else if (opcode === 2 && walkieSpeakers.get(room.id)?.user === user) {
        if (Date.now() - walkieSpeakers.get(room.id).startedAt > 30_000) releaseWalkie(room.id, user, 'timeout');
        else walkieBroadcast(room.id, payload, 2, socket);
      }
    }
  });
  const close = () => {
    if (!walkieClients.delete(socket)) return;
    releaseWalkie(room.id, user, 'disconnect');
    walkieBroadcast(room.id, { type: 'walkie_presence', user, state: 'offline' });
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
