import { createHash, createHmac, randomBytes, scryptSync, timingSafeEqual } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { DatabaseSync } from 'node:sqlite';
import QRCode from 'qrcode';

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
  expoPushEnabled: process.env.SUPACHAT_EXPO_PUSH_ENABLED === 'true',
  sessionSecret: process.env.SUPACHAT_SESSION_SECRET || '',
  papaPasswordHash: process.env.SUPACHAT_PAPA_PASSWORD_HASH || '',
  deviceTokenHashes: {
    albie: process.env.SUPACHAT_ALBIE_DEVICE_TOKEN_HASH || '',
    juju: process.env.SUPACHAT_JUJU_DEVICE_TOKEN_HASH || '',
    papa: process.env.SUPACHAT_PAPA_DEVICE_TOKEN_HASH || '',
    emmanuelle: process.env.SUPACHAT_EMMANUELLE_DEVICE_TOKEN_HASH || '',
    andrew: process.env.SUPACHAT_ANDREW_DEVICE_TOKEN_HASH || '',
    naomie: process.env.SUPACHAT_NAOMIE_DEVICE_TOKEN_HASH || '',
    theo: process.env.SUPACHAT_THEO_DEVICE_TOKEN_HASH || '',
  },
};
const policyVersion = '2026-08-21';

const authentikIdentityMap = new Map([
  ['papa', 'papa'],
  ['albie', 'albie'],
  ['julien', 'juju'],
  ['josee', 'josee'],
  ['maman', 'josee'],
  ['vero', 'mama'],
  ['veronique', 'mama'],
  ['mama', 'mama'],
  ['nico', 'nico'],
  ['miro', 'miro'],
  ['emmanuelle', 'emmanuelle'],
  ['andrew', 'andrew'],
  ['naomie', 'naomie'],
  ['theo', 'theo'],
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
  CREATE TABLE IF NOT EXISTS user_groups (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE
  );
  CREATE TABLE IF NOT EXISTS user_group_members (
    group_id TEXT NOT NULL REFERENCES user_groups(id) ON DELETE CASCADE,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    PRIMARY KEY (group_id, user_id)
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
  CREATE TABLE IF NOT EXISTS pending_user_group_memberships (
    username TEXT NOT NULL,
    group_id TEXT NOT NULL REFERENCES user_groups(id) ON DELETE CASCADE,
    invitation_id TEXT,
    expires_at INTEGER NOT NULL,
    claimed_at INTEGER,
    PRIMARY KEY (username, group_id)
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
  CREATE TABLE IF NOT EXISTS policy_acceptances (
    user_id TEXT NOT NULL REFERENCES users(id),
    version TEXT NOT NULL,
    accepted_at INTEGER NOT NULL,
    PRIMARY KEY (user_id, version)
  );
  CREATE TABLE IF NOT EXISTS user_blocks (
    blocker_id TEXT NOT NULL REFERENCES users(id),
    blocked_user_id TEXT NOT NULL REFERENCES users(id),
    created_at INTEGER NOT NULL,
    PRIMARY KEY (blocker_id, blocked_user_id),
    CHECK (blocker_id <> blocked_user_id)
  );
  CREATE TABLE IF NOT EXISTS moderation_reports (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    reporter_id TEXT NOT NULL REFERENCES users(id),
    message_id INTEGER REFERENCES messages(id),
    reported_user_id TEXT NOT NULL REFERENCES users(id),
    conversation_id TEXT REFERENCES conversations(id),
    category TEXT NOT NULL,
    details TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'reviewed', 'resolved')),
    created_at INTEGER NOT NULL,
    resolved_at INTEGER,
    resolved_by TEXT REFERENCES users(id)
  );
  CREATE TABLE IF NOT EXISTS account_deletion_requests (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT REFERENCES users(id),
    contact TEXT NOT NULL DEFAULT '',
    source TEXT NOT NULL CHECK (source IN ('authenticated', 'public')),
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'verified', 'completed', 'rejected')),
    requested_at INTEGER NOT NULL,
    resolved_at INTEGER,
    resolution_note TEXT NOT NULL DEFAULT ''
  );
  CREATE INDEX IF NOT EXISTS idx_messages_conversation_id ON messages(conversation_id, id);
  CREATE INDEX IF NOT EXISTS idx_moderation_reports_status ON moderation_reports(status, created_at);
  CREATE INDEX IF NOT EXISTS idx_deletion_requests_status ON account_deletion_requests(status, requested_at);
`);
try { db.exec("ALTER TABLE users ADD COLUMN role TEXT NOT NULL DEFAULT 'member' CHECK (role IN ('admin', 'member'))"); } catch (error) {
  if (!String(error).includes('duplicate column')) throw error;
}
try { db.exec("ALTER TABLE conversation_members ADD COLUMN display_name TEXT"); } catch (error) {
  if (!String(error).includes('duplicate column')) throw error;
}
try { db.exec("ALTER TABLE conversation_members ADD COLUMN color_index INTEGER CHECK (color_index BETWEEN 0 AND 15)"); } catch (error) {
  if (!String(error).includes('duplicate column')) throw error;
}
try { db.exec("ALTER TABLE messages ADD COLUMN reply_to_id INTEGER REFERENCES messages(id)"); } catch (error) {
  if (!String(error).includes('duplicate column')) throw error;
}
for (const statement of [
  "ALTER TABLE users ADD COLUMN language_preference TEXT CHECK (language_preference IN ('en','fr'))",
  "ALTER TABLE user_groups ADD COLUMN default_language TEXT NOT NULL DEFAULT 'en' CHECK (default_language IN ('en','fr'))",
  "ALTER TABLE conversations ADD COLUMN group_id TEXT REFERENCES user_groups(id)",
]) {
  try { db.exec(statement); } catch (error) { if (!String(error).includes('duplicate column')) throw error; }
}
db.exec(`CREATE TABLE IF NOT EXISTS device_preferences (
  user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
  language_override TEXT CHECK (language_override IN ('en','fr'))
)`);

const seed = db.prepare('INSERT OR IGNORE INTO users(id, display_name, short_name, kind) VALUES (?, ?, ?, ?)');
seed.run('papa', 'Papa', 'Papa', 'web');
seed.run('albie', 'Albie', 'Albie', 'device');
seed.run('juju', 'Julien', 'Juju', 'device');
seed.run('josee', 'Josée', 'Josée', 'web');
seed.run('emmanuelle', 'Emmanuelle', 'Emma', 'device');
seed.run('andrew', 'Andrew', 'Andrew', 'device');
seed.run('naomie', 'Naomie', 'Naomie', 'device');
seed.run('theo', 'Théo', 'Théo', 'device');
seed.run('mama', 'Véronique', 'Mama', 'web');
seed.run('nico', 'Nico', 'Nico', 'web');
seed.run('miro', 'Miro', 'Miro', 'web');
db.prepare("UPDATE users SET role = 'admin' WHERE id = 'papa'").run();
for (const [id, name, language] of [['family','Family','en'], ['k-buds','KBUDS','en'], ['wolfpack','Wolfpack','fr']]) {
  db.prepare('INSERT OR IGNORE INTO user_groups(id, name, default_language) VALUES (?, ?, ?)').run(id, name, language);
  db.prepare('UPDATE user_groups SET name = ?, default_language = ? WHERE id = ?').run(name, language, id);
}
db.prepare("INSERT OR IGNORE INTO conversations(id, name, kind) VALUES ('family', 'Family', 'shared')").run();
db.prepare("INSERT OR IGNORE INTO conversations(id, name, kind) VALUES ('k-buds', 'K-BUDS', 'room')").run();
db.prepare("INSERT OR IGNORE INTO conversations(id, name, kind) VALUES ('wolfpack', 'Wolfpack', 'room')").run();
for (const id of ['family','k-buds','wolfpack']) db.prepare('UPDATE conversations SET group_id = ? WHERE id = ?').run(id, id);
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('papa');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('albie');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('juju');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('theo');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('family', ?)").run('mama');
for (const userId of ['papa', 'albie', 'juju']) db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('k-buds', ?)").run(userId);
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('k-buds', ?)").run('theo');
db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('k-buds', ?)").run('mama');
for (const userId of ['papa', 'josee', 'emmanuelle', 'andrew', 'naomie']) db.prepare("INSERT OR IGNORE INTO conversation_members(conversation_id, user_id) VALUES ('wolfpack', ?)").run(userId);
db.prepare("UPDATE conversation_members SET display_name = 'Jay' WHERE conversation_id = 'wolfpack' AND user_id = 'papa'").run();
db.prepare("UPDATE conversation_members SET display_name = 'Jay' WHERE conversation_id <> 'family' AND user_id = 'papa'").run();
db.prepare("UPDATE conversation_members SET display_name = 'Mama' WHERE conversation_id = 'family' AND user_id = 'mama'").run();
db.prepare("UPDATE conversation_members SET display_name = 'Véro' WHERE conversation_id = 'k-buds' AND user_id = 'mama'").run();
db.prepare("UPDATE conversation_members SET display_name = 'Maman' WHERE conversation_id = 'wolfpack' AND user_id = 'josee'").run();
db.prepare("UPDATE conversation_members SET display_name = 'Théo' WHERE conversation_id IN ('family', 'k-buds') AND user_id = 'theo'").run();
for (const [groupId, members] of [
  ['family', ['papa','albie','juju','theo','mama']],
  ['k-buds', ['papa','albie','juju','theo','mama','nico','miro']],
  ['wolfpack', ['papa','josee','emmanuelle','andrew','naomie']],
]) for (const userId of members) db.prepare('INSERT OR IGNORE INTO user_group_members(group_id, user_id) VALUES (?, ?)').run(groupId, userId);
// Preserve custom rooms and access from the former per-room model by giving each
// orphan room a group and promoting its direct members into that group.
for (const room of db.prepare("SELECT id, name FROM conversations WHERE kind IN ('shared','room') AND group_id IS NULL").all()) {
  db.prepare('INSERT OR IGNORE INTO user_groups(id, name, default_language) VALUES (?, ?, ?)').run(room.id, room.name, 'en');
  db.prepare('UPDATE conversations SET group_id = ? WHERE id = ?').run(room.id, room.id);
}
db.exec(`INSERT OR IGNORE INTO user_group_members(group_id, user_id)
  SELECT c.group_id, cm.user_id FROM conversation_members cm JOIN conversations c ON c.id = cm.conversation_id WHERE c.group_id IS NOT NULL`);
ensureRoomMembershipMetadata();

const clients = new Map();
const longPolls = new Set();
const walkieClients = new Map();
const walkieSpeakers = new Map();
const loginAttempts = new Map();
const publicDeletionAttempts = new Map();
const assets = new Map([
  ['/app.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'app.css'))]],
  ['/controls.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'controls.css'))]],
  ['/admin.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'admin.css'))]],
  ['/app.js', ['text/javascript; charset=utf-8', readFileSync(join(webRoot, 'app.js'))]],
  ['/supachat-logo.png', ['image/png', readFileSync(join(webRoot, 'supachat-logo.png'))]],
  ['/policy.css', ['text/css; charset=utf-8', readFileSync(join(webRoot, 'policy.css'))]],
]);
const loginHtml = readFileSync(join(webRoot, 'login.html'));
const appHtml = readFileSync(join(webRoot, 'index.html'));
const privacyHtml = readFileSync(join(webRoot, 'privacy.html'));
const termsHtml = readFileSync(join(webRoot, 'terms.html'));
const deleteAccountHtml = readFileSync(join(webRoot, 'delete-account.html'));

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

function requestIp(req) {
  return String(req.headers['x-forwarded-for'] || req.socket.remoteAddress || 'unknown').split(',')[0].trim();
}

function authentikUserId(username, uid) {
  return authentikIdentityMap.get(String(username).toLowerCase())
    || `web-${createHash('sha256').update(String(uid)).digest('hex').slice(0, 16)}`;
}

function proxyHeaderText(value) {
  const text = String(value || '');
  if (!/[ÃÂ]/.test(text)) return text;
  const repaired = Buffer.from(text, 'latin1').toString('utf8');
  return repaired.includes('\uFFFD') ? text : repaired;
}

function randomPaletteIndex(indices) {
  return indices[randomBytes(4).readUInt32BE(0) % indices.length];
}

function ensureRoomMembershipMetadata() {
  db.exec(`INSERT OR IGNORE INTO conversation_members(conversation_id, user_id)
    SELECT c.id, gm.user_id FROM conversations c JOIN user_group_members gm ON gm.group_id = c.group_id
    WHERE c.kind IN ('shared','room')`);
  const rooms = db.prepare("SELECT id FROM conversations WHERE kind IN ('shared','room')").all();
  const unassignedQuery = db.prepare(`SELECT cm.user_id FROM conversation_members cm
    JOIN conversations c ON c.id=cm.conversation_id JOIN user_group_members gm ON gm.group_id=c.group_id AND gm.user_id=cm.user_id
    WHERE cm.conversation_id=? AND cm.color_index IS NULL ORDER BY cm.rowid`);
  const usedQuery = db.prepare(`SELECT cm.color_index FROM conversation_members cm
    JOIN conversations c ON c.id=cm.conversation_id JOIN user_group_members gm ON gm.group_id=c.group_id AND gm.user_id=cm.user_id
    WHERE cm.conversation_id=? AND cm.color_index IS NOT NULL`);
  const assign = db.prepare('UPDATE conversation_members SET color_index=? WHERE conversation_id=? AND user_id=?');
  for (const room of rooms) {
    const used = new Set(usedQuery.all(room.id).map((row) => row.color_index));
    for (const member of unassignedQuery.all(room.id)) {
      const available = Array.from({ length: 16 }, (_, index) => index).filter((index) => !used.has(index));
      const colorIndex = randomPaletteIndex(available.length ? available : Array.from({ length: 16 }, (_, index) => index));
      assign.run(colorIndex, room.id, member.user_id);
      if (available.length) used.add(colorIndex);
    }
  }
}

function claimPendingMemberships(userId, username) {
  const now = Date.now();
  const pending = db.prepare('SELECT conversation_id FROM pending_room_memberships WHERE username = ? AND claimed_at IS NULL AND expires_at > ?').all(username, now);
  const grant = db.prepare(`INSERT OR IGNORE INTO user_group_members(group_id, user_id)
    SELECT group_id, ? FROM conversations WHERE id = ? AND group_id IS NOT NULL`);
  const claim = db.prepare('UPDATE pending_room_memberships SET claimed_at = ? WHERE username = ? AND conversation_id = ?');
  for (const row of pending) { grant.run(userId, row.conversation_id); claim.run(now, username, row.conversation_id); }
  const pendingGroups = db.prepare('SELECT group_id FROM pending_user_group_memberships WHERE username = ? AND claimed_at IS NULL AND expires_at > ?').all(username, now);
  const grantGroup = db.prepare('INSERT OR IGNORE INTO user_group_members(group_id, user_id) VALUES (?, ?)');
  const claimGroup = db.prepare('UPDATE pending_user_group_memberships SET claimed_at = ? WHERE username = ? AND group_id = ?');
  for (const row of pendingGroups) { grantGroup.run(row.group_id, userId); claimGroup.run(now, username, row.group_id); }
  ensureRoomMembershipMetadata();
}

function roomsFor(userId) {
  return db.prepare(`SELECT c.id, c.name, c.group_id, g.name AS group_name, g.default_language,
      COALESCE(dp.language_override, u.language_preference, g.default_language, 'en') AS effective_language,
      COALESCE(MAX(m.id), 0) AS latest_message_id FROM conversations c
    JOIN user_groups g ON g.id = c.group_id JOIN user_group_members gm ON gm.group_id = g.id
    JOIN users u ON u.id = gm.user_id LEFT JOIN device_preferences dp ON dp.user_id = u.id
    LEFT JOIN messages m ON m.conversation_id = c.id
    WHERE gm.user_id = ? GROUP BY c.id, c.name, c.group_id, g.name, g.default_language ORDER BY c.name`).all(userId);
}

function authorizedRoom(userId, requested) {
  const roomId = String(requested || '').trim().toLowerCase();
  if (!roomId) return null;
  return db.prepare(`SELECT c.id, c.name, c.group_id, g.default_language FROM conversations c
    JOIN user_groups g ON g.id=c.group_id JOIN user_group_members gm ON gm.group_id=g.id
    WHERE c.id = ? AND gm.user_id = ?`).get(roomId, userId);
}

function policyAcceptedAt(userId) {
  return db.prepare('SELECT accepted_at FROM policy_acceptances WHERE user_id = ? AND version = ?').get(userId, policyVersion)?.accepted_at || null;
}

function requiresPolicyAcceptance(req) {
  return !deviceUser(req);
}

function webUser(req) {
  const host = requestHost(req);
  const authentikUid = String(req.headers['x-authentik-uid'] || '').trim();
  const authentikUsername = String(req.headers['x-authentik-username'] || '').trim();
  if (host === config.portalHost && authentikUid && authentikUsername) {
    const normalizedUsername = authentikUsername.toLowerCase();
    const id = authentikUserId(normalizedUsername, authentikUid);
    const displayName = proxyHeaderText(req.headers['x-authentik-name'] || authentikUsername).trim().slice(0, 80) || authentikUsername.slice(0, 80);
    const shortName = displayName.split(/\s+/)[0].slice(0, 12) || 'Friend';
    db.prepare(`
      INSERT INTO users(id, display_name, short_name, kind) VALUES (?, ?, ?, 'web')
      ON CONFLICT(id) DO UPDATE SET display_name = excluded.display_name, short_name = excluded.short_name
    `).run(id, displayName, shortName);
    claimPendingMemberships(id, normalizedUsername);
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
    JOIN user_group_members gm ON gm.user_id = nd.user_id JOIN conversations c ON c.group_id = gm.group_id
    WHERE nd.provider = 'expo' AND nd.enabled = 1 AND nd.user_id <> ? AND c.id = ?
      AND NOT EXISTS (SELECT 1 FROM user_blocks b WHERE b.blocker_id = nd.user_id AND b.blocked_user_id = ?)`).all(message.author_id, message.conversation_id, message.author_id);
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

function messageRows(roomId, after = 0, limit = 100, viewerId = null) {
  const bounded = Math.min(Math.max(limit, 1), 100);
  const rows = after > 0 ? db.prepare(`
    SELECT m.id, m.client_id, m.conversation_id, m.author_id, COALESCE(cm.display_name, u.display_name) AS author_name, cm.color_index AS author_color, m.reply_to_id,
           u.short_name AS author_short, m.type, m.body, m.created_at
    FROM messages m JOIN users u ON u.id = m.author_id
      LEFT JOIN conversation_members cm ON cm.conversation_id = m.conversation_id AND cm.user_id = m.author_id
    WHERE m.conversation_id = ? AND m.id > ?
      AND (? IS NULL OR NOT EXISTS (SELECT 1 FROM user_blocks b WHERE b.blocker_id = ? AND b.blocked_user_id = m.author_id))
    ORDER BY m.id ASC LIMIT ?
  `).all(roomId, after, viewerId, viewerId, bounded) : db.prepare(`
    SELECT * FROM (
      SELECT m.id, m.client_id, m.conversation_id, m.author_id, COALESCE(cm.display_name, u.display_name) AS author_name, cm.color_index AS author_color, m.reply_to_id,
             u.short_name AS author_short, m.type, m.body, m.created_at
      FROM messages m JOIN users u ON u.id = m.author_id
        LEFT JOIN conversation_members cm ON cm.conversation_id = m.conversation_id AND cm.user_id = m.author_id
      WHERE m.conversation_id = ?
        AND (? IS NULL OR NOT EXISTS (SELECT 1 FROM user_blocks b WHERE b.blocker_id = ? AND b.blocked_user_id = m.author_id))
      ORDER BY m.id DESC LIMIT ?
    ) ORDER BY id ASC
  `).all(roomId, viewerId, viewerId, bounded);
  return rows.map((row) => ({
    ...row, conversation_name: db.prepare('SELECT name FROM conversations WHERE id = ?').get(row.conversation_id)?.name,
    reply_to: row.reply_to_id ? db.prepare(`SELECT original.id,original.author_id,COALESCE(alias.display_name,author.display_name) AS author_name,
      original.body,original.type,alias.color_index AS author_color FROM messages original JOIN users author ON author.id=original.author_id
      LEFT JOIN conversation_members alias ON alias.conversation_id=original.conversation_id AND alias.user_id=original.author_id
      WHERE original.id=? AND original.conversation_id=?`).get(row.reply_to_id,row.conversation_id) || null : null,
    receipts: db.prepare('SELECT user_id, state, updated_at FROM receipts WHERE message_id = ?').all(row.id),
    voice: row.type === 'voice' ? db.prepare(`
      SELECT mime_type, sample_rate, sample_count, duration_ms, byte_length
      FROM voice_clips WHERE message_id = ?
    `).get(row.id) : undefined,
  }));
}

function presenceRows(roomId) {
  const now = Date.now();
  return db.prepare(`
    SELECT u.id, COALESCE(cm.display_name, u.display_name) AS display_name, u.short_name, cm.color_index, p.last_seen_at, p.connected
    FROM conversations c JOIN user_group_members gm ON gm.group_id = c.group_id JOIN users u ON u.id = gm.user_id
    LEFT JOIN conversation_members cm ON cm.user_id = u.id AND cm.conversation_id = c.id LEFT JOIN presence p ON p.user_id = u.id
    WHERE u.revoked_at IS NULL AND c.id = ? ORDER BY COALESCE(cm.display_name, u.display_name)
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
  for (const [res, clientUser] of clients) {
    if (event === 'message' && db.prepare('SELECT 1 FROM user_blocks WHERE blocker_id = ? AND blocked_user_id = ?').get(clientUser, payload.author_id)) continue;
    res.write(packet);
  }
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
  const sourceUser = except ? walkieClients.get(except)?.user : value?.user;
  for (const [socket, client] of walkieClients) {
    if (client.roomId !== roomId || socket === except || socket.destroyed) continue;
    if (sourceUser && db.prepare('SELECT 1 FROM user_blocks WHERE blocker_id = ? AND blocked_user_id = ?').get(client.user, sourceUser)) continue;
    socket.write(frame);
  }
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
    const publicPages = new Map([
      ['/privacy', privacyHtml], ['/privacy.html', privacyHtml],
      ['/terms', termsHtml], ['/terms.html', termsHtml],
      ['/delete-account', deleteAccountHtml], ['/delete-account.html', deleteAccountHtml],
    ]);
    if (publicPages.has(url.pathname) && req.method === 'GET') {
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'public, max-age=300' });
      return res.end(publicPages.get(url.pathname));
    }
    if (url.pathname === '/api/account/deletion/public' && req.method === 'POST') {
      if (!sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const ip = requestIp(req);
      const attempts = publicDeletionAttempts.get(ip) || [];
      const recent = attempts.filter((time) => time > Date.now() - 24 * 60 * 60_000);
      if (recent.length >= 3) return json(res, 429, { error: 'try_later' });
      const payload = await body(req);
      const contact = String(payload.contact || '').trim().slice(0, 160);
      const website = String(payload.website || '').trim();
      if (website) return json(res, 200, { ok: true });
      if (!contact || !(/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(contact) || /^[a-z0-9][a-z0-9._-]{1,31}$/i.test(contact))) {
        return json(res, 400, { error: 'invalid_contact' });
      }
      db.prepare("INSERT INTO account_deletion_requests(contact, source, requested_at) VALUES (?, 'public', ?)").run(contact, Date.now());
      publicDeletionAttempts.set(ip, [...recent, Date.now()]);
      return json(res, 201, { ok: true });
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
      claimPendingMemberships(id, username);
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
      const profile = db.prepare('SELECT id, display_name, short_name, role, language_preference FROM users WHERE id = ?').get(user);
      return json(res, 200, {
        user: profile,
        rooms: roomsFor(user),
        auth: deviceUser(req) ? 'device' : nativeUser(req) ? 'native' : (String(req.headers['x-authentik-uid'] || '') ? 'authentik' : 'legacy'),
        policy: { version: policyVersion, accepted_at: policyAcceptedAt(user) },
      });
    }

    if (url.pathname === '/api/rooms' && req.method === 'GET') return json(res, 200, { rooms: roomsFor(user) });

    if (url.pathname === '/api/preferences/language' && req.method === 'PATCH') {
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req);
      const language = payload.language == null || payload.language === 'auto' ? null : String(payload.language);
      if (language !== null && !['en','fr'].includes(language)) return json(res, 400, { error: 'invalid_language' });
      if (deviceUser(req)) {
        db.prepare(`INSERT INTO device_preferences(user_id, language_override) VALUES (?, ?)
          ON CONFLICT(user_id) DO UPDATE SET language_override=excluded.language_override`).run(user, language);
      } else db.prepare('UPDATE users SET language_preference = ? WHERE id = ?').run(language, user);
      return json(res, 200, { language, rooms: roomsFor(user) });
    }

    if (url.pathname === '/api/policy/accept' && req.method === 'POST') {
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req);
      if (payload.version !== policyVersion) return json(res, 409, { error: 'policy_version_changed', policy_version: policyVersion });
      const acceptedAt = Date.now();
      db.prepare('INSERT OR REPLACE INTO policy_acceptances(user_id, version, accepted_at) VALUES (?, ?, ?)').run(user, policyVersion, acceptedAt);
      return json(res, 200, { ok: true, version: policyVersion, accepted_at: acceptedAt });
    }

    if (url.pathname === '/api/account/deletion' && req.method === 'POST') {
      if (deviceUser(req)) return json(res, 403, { error: 'interactive_session_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const existing = db.prepare("SELECT id, requested_at FROM account_deletion_requests WHERE user_id = ? AND status IN ('open','verified') ORDER BY id DESC LIMIT 1").get(user);
      if (existing) return json(res, 200, { ok: true, request_id: existing.id, requested_at: existing.requested_at });
      const result = db.prepare("INSERT INTO account_deletion_requests(user_id, source, requested_at) VALUES (?, 'authenticated', ?)").run(user, Date.now());
      const created = db.prepare('SELECT id, requested_at FROM account_deletion_requests WHERE id = ?').get(result.lastInsertRowid);
      return json(res, 201, { ok: true, request_id: created.id, requested_at: created.requested_at });
    }

    if (url.pathname === '/api/moderation/reports' && req.method === 'POST') {
      if (deviceUser(req)) return json(res, 403, { error: 'interactive_session_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req);
      const messageId = Number(payload.message_id);
      const category = String(payload.category || '').trim().toLowerCase();
      const details = String(payload.details || '').trim().slice(0, 500);
      if (!Number.isInteger(messageId) || !['harassment', 'hate_or_abuse', 'sexual_or_unsafe', 'spam', 'other'].includes(category)) {
        return json(res, 400, { error: 'invalid_report' });
      }
      const reported = db.prepare(`SELECT m.id, m.author_id, m.conversation_id FROM messages m
        JOIN conversations c ON c.id=m.conversation_id JOIN user_group_members gm ON gm.group_id=c.group_id
        WHERE m.id = ? AND gm.user_id = ?`).get(messageId, user);
      if (!reported) return json(res, 404, { error: 'message_not_found' });
      if (reported.author_id === user) return json(res, 400, { error: 'cannot_report_self' });
      const duplicate = db.prepare("SELECT id FROM moderation_reports WHERE reporter_id = ? AND message_id = ? AND status <> 'resolved'").get(user, messageId);
      if (duplicate) return json(res, 200, { ok: true, report_id: duplicate.id });
      const result = db.prepare(`INSERT INTO moderation_reports(reporter_id, message_id, reported_user_id, conversation_id, category, details, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)`).run(user, messageId, reported.author_id, reported.conversation_id, category, details, Date.now());
      return json(res, 201, { ok: true, report_id: Number(result.lastInsertRowid) });
    }

    if (url.pathname === '/api/moderation/blocks' && req.method === 'GET') {
      if (deviceUser(req)) return json(res, 403, { error: 'interactive_session_required' });
      const blocked = db.prepare(`SELECT u.id, u.display_name, u.short_name, b.created_at FROM user_blocks b
        JOIN users u ON u.id = b.blocked_user_id WHERE b.blocker_id = ? ORDER BY u.display_name`).all(user);
      return json(res, 200, { blocked });
    }
    if (url.pathname === '/api/moderation/blocks' && req.method === 'POST') {
      if (deviceUser(req)) return json(res, 403, { error: 'interactive_session_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req); const blockedUserId = String(payload.user_id || '');
      if (!blockedUserId || blockedUserId === user || !db.prepare('SELECT 1 FROM users WHERE id = ? AND revoked_at IS NULL').get(blockedUserId)) {
        return json(res, 400, { error: 'invalid_block' });
      }
      db.prepare('INSERT OR IGNORE INTO user_blocks(blocker_id, blocked_user_id, created_at) VALUES (?, ?, ?)').run(user, blockedUserId, Date.now());
      return json(res, 200, { ok: true });
    }
    const unblockPath = url.pathname.match(/^\/api\/moderation\/blocks\/([^/]+)$/);
    if (unblockPath && req.method === 'DELETE') {
      if (deviceUser(req)) return json(res, 403, { error: 'interactive_session_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      db.prepare('DELETE FROM user_blocks WHERE blocker_id = ? AND blocked_user_id = ?').run(user, decodeURIComponent(unblockPath[1]));
      return json(res, 200, { ok: true });
    }

    const admin = db.prepare('SELECT role FROM users WHERE id = ?').get(user)?.role === 'admin';
    if (url.pathname === '/api/admin/compliance' && req.method === 'GET') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      const reports = db.prepare(`SELECT r.id, r.reporter_id, reporter.display_name AS reporter_name, r.message_id,
        r.reported_user_id, reported.display_name AS reported_user_name, r.conversation_id, r.category, r.details, r.status, r.created_at
        FROM moderation_reports r JOIN users reporter ON reporter.id = r.reporter_id JOIN users reported ON reported.id = r.reported_user_id
        WHERE r.status <> 'resolved' ORDER BY r.created_at ASC`).all();
      const deletionRequests = db.prepare(`SELECT d.id, d.user_id, u.display_name, d.contact, d.source, d.status, d.requested_at
        FROM account_deletion_requests d LEFT JOIN users u ON u.id = d.user_id
        WHERE d.status NOT IN ('completed','rejected') ORDER BY d.requested_at ASC`).all();
      return json(res, 200, { reports, deletion_requests: deletionRequests });
    }
    const reportStatusPath = url.pathname.match(/^\/api\/admin\/compliance\/reports\/(\d+)$/);
    if (reportStatusPath && req.method === 'PATCH') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req); const status = String(payload.status || '');
      if (!['reviewed', 'resolved'].includes(status)) return json(res, 400, { error: 'invalid_status' });
      const result = db.prepare('UPDATE moderation_reports SET status = ?, resolved_at = ?, resolved_by = ? WHERE id = ?').run(status, status === 'resolved' ? Date.now() : null, user, Number(reportStatusPath[1]));
      return result.changes ? json(res, 200, { ok: true }) : json(res, 404, { error: 'report_not_found' });
    }
    const deletionStatusPath = url.pathname.match(/^\/api\/admin\/compliance\/deletions\/(\d+)$/);
    if (deletionStatusPath && req.method === 'PATCH') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req); const status = String(payload.status || '');
      const note = String(payload.resolution_note || '').trim().slice(0, 500);
      if (!['verified', 'completed', 'rejected'].includes(status)) return json(res, 400, { error: 'invalid_status' });
      const result = db.prepare('UPDATE account_deletion_requests SET status = ?, resolved_at = ?, resolution_note = ? WHERE id = ?').run(status, ['completed','rejected'].includes(status) ? Date.now() : null, note, Number(deletionStatusPath[1]));
      return result.changes ? json(res, 200, { ok: true }) : json(res, 404, { error: 'deletion_request_not_found' });
    }
    if (url.pathname === '/api/admin/rooms' && req.method === 'GET') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      const rooms = db.prepare(`SELECT c.id, c.name, c.group_id, g.name AS group_name, g.default_language, COUNT(gm.user_id) AS member_count
        FROM conversations c JOIN user_groups g ON g.id=c.group_id LEFT JOIN user_group_members gm ON gm.group_id=g.id
        WHERE c.kind IN ('shared','room') GROUP BY c.id ORDER BY c.name`).all();
      const members = db.prepare(`SELECT cm.conversation_id, u.id, COALESCE(cm.display_name, u.display_name) AS display_name, u.short_name, u.kind, u.role
        FROM conversations c JOIN user_group_members gm ON gm.group_id=c.group_id JOIN users u ON u.id=gm.user_id
        LEFT JOIN conversation_members cm ON cm.conversation_id=c.id AND cm.user_id=u.id ORDER BY COALESCE(cm.display_name, u.display_name)`).all();
      const users = db.prepare('SELECT id, display_name, short_name, kind, role FROM users WHERE revoked_at IS NULL ORDER BY display_name').all();
      return json(res, 200, { rooms: rooms.map((room) => ({ ...room, members: members.filter((member) => member.conversation_id === room.id) })), users });
    }
    if (url.pathname === '/api/admin/rooms' && req.method === 'POST') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req); const name = String(payload.name || '').trim(); const groupId = String(payload.group_id || '').trim();
      const id = String(payload.id || name).trim().toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 40);
      if (!/^[a-z0-9][a-z0-9-]{1,39}$/.test(id) || name.length < 2 || name.length > 60) return json(res, 400, { error: 'invalid_room' });
      if (!db.prepare('SELECT 1 FROM user_groups WHERE id = ?').get(groupId)) return json(res, 400, { error: 'invalid_group' });
      try { db.prepare("INSERT INTO conversations(id, name, kind, group_id) VALUES (?, ?, 'room', ?)").run(id, name, groupId); }
      catch (error) { if (String(error).includes('UNIQUE')) return json(res, 409, { error: 'room_exists' }); throw error; }
      ensureRoomMembershipMetadata();
      return json(res, 201, { room: { id, name, group_id: groupId } });
    }
    const memberPath = url.pathname.match(/^\/api\/admin\/rooms\/([^/]+)\/members(?:\/([^/]+))?$/);
    if (memberPath && ['POST','DELETE'].includes(req.method)) {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const roomId = decodeURIComponent(memberPath[1]);
      if (!db.prepare("SELECT 1 FROM conversations WHERE id = ? AND kind IN ('shared','room')").get(roomId)) return json(res, 404, { error: 'room_not_found' });
      return json(res, 409, { error: 'membership_managed_by_group' });
    }

    if (['/api/admin/groups','/api/admin/user-groups'].includes(url.pathname) && req.method === 'GET') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      const groups = db.prepare(`SELECT g.id, g.name, g.default_language, COUNT(gm.user_id) AS member_count FROM user_groups g
        LEFT JOIN user_group_members gm ON gm.group_id = g.id GROUP BY g.id ORDER BY g.name`).all();
      const members = db.prepare(`SELECT gm.group_id, u.id,
        COALESCE((SELECT cm.display_name FROM conversations c JOIN conversation_members cm ON cm.conversation_id=c.id
          WHERE c.group_id=gm.group_id AND cm.user_id=u.id AND cm.display_name IS NOT NULL LIMIT 1),u.display_name) AS display_name,
        u.display_name AS account_name,u.short_name,u.kind,u.role FROM user_group_members gm
        JOIN users u ON u.id = gm.user_id ORDER BY display_name`).all();
      const users = db.prepare('SELECT id, display_name, short_name, kind, role FROM users WHERE revoked_at IS NULL ORDER BY display_name').all();
      const rooms = db.prepare("SELECT id, name, group_id FROM conversations WHERE kind IN ('shared','room') ORDER BY name").all();
      return json(res, 200, { groups: groups.map((group) => ({ ...group, members: members.filter((member) => member.group_id === group.id), rooms: rooms.filter((room) => room.group_id === group.id) })), users });
    }
    if (['/api/admin/groups','/api/admin/user-groups'].includes(url.pathname) && req.method === 'POST') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req); const name = String(payload.name || '').trim(); const defaultLanguage = String(payload.default_language || 'en');
      const id = String(payload.id || name).trim().toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 40);
      if (!/^[a-z0-9][a-z0-9-]{1,39}$/.test(id) || name.length < 2 || name.length > 60 || !['en','fr'].includes(defaultLanguage)) return json(res, 400, { error: 'invalid_user_group' });
      try { db.prepare('INSERT INTO user_groups(id, name, default_language) VALUES (?, ?, ?)').run(id, name, defaultLanguage); }
      catch (error) { if (String(error).includes('UNIQUE')) return json(res, 409, { error: 'user_group_exists' }); throw error; }
      return json(res, 201, { group: { id, name, default_language: defaultLanguage, members: [], rooms: [] } });
    }
    const userGroupMemberPath = url.pathname.match(/^\/api\/admin\/(?:groups|user-groups)\/([^/]+)\/members(?:\/([^/]+))?$/);
    if (userGroupMemberPath && ['POST','DELETE','PATCH'].includes(req.method)) {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const groupId = decodeURIComponent(userGroupMemberPath[1]);
      if (!db.prepare('SELECT 1 FROM user_groups WHERE id = ?').get(groupId)) return json(res, 404, { error: 'user_group_not_found' });
      if (req.method === 'POST') {
        const payload = await body(req); const memberId = String(payload.user_id || '');
        if (!db.prepare('SELECT 1 FROM users WHERE id = ? AND revoked_at IS NULL').get(memberId)) return json(res, 404, { error: 'user_not_found' });
        db.prepare('INSERT OR IGNORE INTO user_group_members(group_id, user_id) VALUES (?, ?)').run(groupId, memberId);
        ensureRoomMembershipMetadata();
        return json(res, 200, { ok: true });
      }
      if (req.method === 'PATCH') {
        const memberId = decodeURIComponent(userGroupMemberPath[2] || '');
        if (!db.prepare('SELECT 1 FROM user_group_members WHERE group_id=? AND user_id=?').get(groupId, memberId)) return json(res, 404, { error: 'membership_not_found' });
        const payload = await body(req); const alias = String(payload.display_name || '').trim();
        if (alias.length > 80) return json(res, 400, { error: 'invalid_alias' });
        ensureRoomMembershipMetadata();
        db.prepare(`UPDATE conversation_members SET display_name=? WHERE user_id=? AND conversation_id IN
          (SELECT id FROM conversations WHERE group_id=?)`).run(alias || null, memberId, groupId);
        return json(res, 200, { ok: true, display_name: alias || null });
      }
      db.prepare('DELETE FROM user_group_members WHERE group_id = ? AND user_id = ?').run(groupId, decodeURIComponent(userGroupMemberPath[2] || ''));
      return json(res, 200, { ok: true });
    }
    const groupSettingsPath = url.pathname.match(/^\/api\/admin\/groups\/([^/]+)$/);
    if (groupSettingsPath && req.method === 'PATCH') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req); const language = String(payload.default_language || '');
      if (!['en','fr'].includes(language)) return json(res, 400, { error: 'invalid_language' });
      const result = db.prepare('UPDATE user_groups SET default_language = ? WHERE id = ?').run(language, decodeURIComponent(groupSettingsPath[1]));
      return result.changes ? json(res, 200, { ok: true }) : json(res, 404, { error: 'group_not_found' });
    }

    if (url.pathname === '/api/admin/invitations' && req.method === 'POST') {
      if (!admin) return json(res, 403, { error: 'admin_required' });
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      const payload = await body(req);
      const username = String(payload.username || '').trim().toLowerCase();
      const displayName = String(payload.display_name || '').trim();
      const email = String(payload.email || '').trim().toLowerCase();
      const requestedRoomIds = Array.isArray(payload.room_ids) ? payload.room_ids : payload.room_id ? [payload.room_id] : [];
      const roomIds = [...new Set(requestedRoomIds.map((value) => String(value).trim().toLowerCase()).filter(Boolean))];
      const userGroupId = String(payload.user_group_id || '').trim().toLowerCase();
      const requestedGroupIds = Array.isArray(payload.group_ids) ? payload.group_ids : [];
      const groupIds = [...new Set([...requestedGroupIds.map((value) => String(value).trim().toLowerCase()), userGroupId,
        ...roomIds.map((roomId) => db.prepare('SELECT group_id FROM conversations WHERE id = ?').get(roomId)?.group_id)].filter(Boolean))];
      const validRooms = roomIds.every((roomId) => db.prepare("SELECT 1 FROM conversations WHERE id = ? AND kind IN ('shared','room')").get(roomId));
      const validGroups = groupIds.every((groupId) => db.prepare('SELECT 1 FROM user_groups WHERE id = ?').get(groupId));
      if (!validRooms || !validGroups || !groupIds.length || !/^[a-z0-9][a-z0-9._-]{1,31}$/.test(username) || !displayName || displayName.length > 80 || (email && !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email))) {
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
          body: JSON.stringify({ name: `supachat_${username}_${Date.now()}`, expires: new Date(expiresAt).toISOString(), fixed_data: { username, name: displayName, email }, single_use: true, flow: inviteFlowId }),
        });
        if (!invitationResponse.ok) {
          console.error('Authentik invitation creation failed', invitationResponse.status, await invitationResponse.text());
          return json(res, 502, { error: 'identity_provider_failed' });
        }
        invitation = await invitationResponse.json();
      }
      const pendingRoom = db.prepare(`INSERT INTO pending_room_memberships(username, conversation_id, display_name, invitation_id, expires_at, claimed_at)
        VALUES (?, ?, ?, ?, ?, NULL) ON CONFLICT(username, conversation_id) DO UPDATE SET display_name=excluded.display_name,
        invitation_id=excluded.invitation_id, expires_at=excluded.expires_at, claimed_at=NULL`);
      for (const roomId of roomIds) pendingRoom.run(username, roomId, displayName, invitation.pk, expiresAt);
      const pendingGroup = db.prepare(`INSERT INTO pending_user_group_memberships(username, group_id, invitation_id, expires_at, claimed_at)
        VALUES (?, ?, ?, ?, NULL) ON CONFLICT(username, group_id) DO UPDATE SET invitation_id=excluded.invitation_id,
        expires_at=excluded.expires_at, claimed_at=NULL`);
      for (const groupId of groupIds) pendingGroup.run(username, groupId, invitation.pk, expiresAt);
      const enrollmentSlug = 'supachat-invitation-enrollment';
      const enrollmentUrl = new URL(`https://auth.${config.portalHost}/if/flow/${enrollmentSlug}/`);
      enrollmentUrl.searchParams.set('itoken', invitation.pk);
      const portalUrl = new URL(`https://${config.portalHost}/`); portalUrl.searchParams.set('welcome', '1');
      const firstRoom = roomIds[0] || db.prepare("SELECT id FROM conversations WHERE group_id = ? AND kind IN ('shared','room') ORDER BY name LIMIT 1").get(groupIds[0])?.id;
      if (firstRoom) portalUrl.searchParams.set('room', firstRoom);
      enrollmentUrl.searchParams.set('next', portalUrl.toString());
      const invitationUrl = enrollmentUrl.toString();
      const qrDataUrl = await QRCode.toDataURL(invitationUrl, { errorCorrectionLevel: 'M', margin: 2, width: 320, color: { dark: '#10200f', light: '#f6f0dc' } });
      return json(res, 201, { url: invitationUrl, qr_data_url: qrDataUrl, username, group_ids: groupIds, room_ids: roomIds, expires_at: expiresAt });
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
      if (!url.searchParams.get('room')) return json(res, 400, { error: 'room_required' });
      const room = authorizedRoom(user, url.searchParams.get('room'));
      if (!room) return json(res, 403, { error: 'room_forbidden' });
      const messages = messageRows(room.id, Number(url.searchParams.get('after') || 0), Number(url.searchParams.get('limit') || 100), user);
      if (webUser(req)) {
        for (const message of messages) {
          if (message.author_id !== user && setReceipt(message.id, user, 'read')) {
            publish('receipt', { message_id: message.id, user_id: user, state: 'read', updated_at: Date.now() });
          }
        }
      }
      return json(res, 200, { room, messages: messageRows(room.id, Number(url.searchParams.get('after') || 0), Number(url.searchParams.get('limit') || 100), user) });
    }
    if (url.pathname === '/api/messages' && req.method === 'POST') {
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      if (requiresPolicyAcceptance(req) && !policyAcceptedAt(user)) return json(res, 428, { error: 'policy_acceptance_required', policy_version: policyVersion });
      const payload = await body(req);
      if (!payload.room_id) return json(res, 400, { error: 'room_required' });
      const room = authorizedRoom(user, payload.room_id);
      if (!room) return json(res, 403, { error: 'room_forbidden' });
      const text = typeof payload.body === 'string' ? payload.body.trim() : '';
      const clientId = String(payload.client_id || '').slice(0, 80);
      const replyToId = payload.reply_to_id == null ? null : Number(payload.reply_to_id);
      if (!text || [...text].length > 140 || !clientId) return json(res, 400, { error: 'invalid_message', max_length: 140 });
      if (replyToId !== null && (!Number.isInteger(replyToId) || !db.prepare('SELECT 1 FROM messages WHERE id=? AND conversation_id=?').get(replyToId, room.id))) return json(res, 400, { error: 'invalid_reply' });
      const existing = db.prepare('SELECT id, conversation_id, body, type, reply_to_id FROM messages WHERE author_id = ? AND client_id = ?').get(user, clientId);
      if (existing && (existing.conversation_id !== room.id || existing.body !== text || existing.type !== 'text' || existing.reply_to_id !== replyToId)) {
        return json(res, 409, { error: 'client_id_conflict' });
      }
      let result;
      try {
        result = db.prepare('INSERT INTO messages(conversation_id, author_id, client_id, body, created_at, reply_to_id) VALUES (?, ?, ?, ?, ?, ?)').run(room.id, user, clientId, text, Date.now(), replyToId);
      } catch (error) {
        if (!String(error).includes('UNIQUE')) throw error;
      }
      const row = db.prepare('SELECT id FROM messages WHERE author_id = ? AND client_id = ?').get(user, clientId);
      setReceipt(row.id, user, 'server');
      const message = messageRows(room.id, row.id - 1, 1, user)[0];
      if (result) { publish('message', message); sendExpoNotifications(message); }
      return json(res, result ? 201 : 200, { message });
    }
    if (url.pathname === '/api/voice' && req.method === 'POST') {
      if (webUser(req) && !sameOrigin(req)) return json(res, 403, { error: 'origin_rejected' });
      if (requiresPolicyAcceptance(req) && !policyAcceptedAt(user)) return json(res, 428, { error: 'policy_acceptance_required', policy_version: policyVersion });
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
      let inserted = false;
      try {
        const result = db.prepare(`
          INSERT INTO messages(conversation_id, author_id, client_id, type, body, created_at)
          VALUES (?, ?, ?, 'voice', '[voice]', ?)
        `).run(room.id, user, clientId, Date.now());
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
      const message = messageRows(room.id, row.id - 1, 1, user)[0];
      if (inserted) { publish('message', message); sendExpoNotifications(message); }
      return json(res, inserted ? 201 : 200, { message });
    }
    const voiceMatch = url.pathname.match(/^\/api\/voice\/(\d+)\/audio$/);
    if (voiceMatch && req.method === 'GET') {
      const clip = db.prepare(`SELECT vc.* FROM voice_clips vc JOIN messages m ON m.id = vc.message_id
        JOIN conversations c ON c.id=m.conversation_id JOIN user_group_members gm ON gm.group_id=c.group_id
        WHERE vc.message_id = ? AND gm.user_id = ?
          AND NOT EXISTS (SELECT 1 FROM user_blocks b WHERE b.blocker_id = ? AND b.blocked_user_id = m.author_id)`).get(Number(voiceMatch[1]), user, user);
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
      if (!db.prepare('SELECT 1 FROM messages m JOIN conversations c ON c.id=m.conversation_id JOIN user_group_members gm ON gm.group_id=c.group_id WHERE m.id = ? AND gm.user_id = ?').get(messageId, user)) return json(res, 404, { error: 'message_not_found' });
      setReceipt(messageId, user, payload.state);
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
      clients.set(res, user);
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
      let messages = messageRows(room.id, after, limit, user);
      let receipts = receiptRows(room.id, receiptsAfter, limit);
      if (shouldWait && !messages.length && !receipts.length) {
        await new Promise((resolve) => {
          const timer = setTimeout(() => { longPolls.delete(wake); resolve(); }, 25_000);
          const wake = () => { clearTimeout(timer); resolve(); };
          longPolls.add(wake);
          res.on('close', () => { clearTimeout(timer); longPolls.delete(wake); resolve(); });
        });
        messages = messageRows(room.id, after, limit, user);
        receipts = receiptRows(room.id, receiptsAfter, limit);
      }
      for (const message of messages) {
        if (message.author_id !== user && setReceipt(message.id, user, 'delivered')) {
          publish('receipt', { message_id: message.id, user_id: user, state: 'delivered', updated_at: Date.now() });
        }
      }
      return json(res, 200, { server_time: Date.now(), room, rooms: roomsFor(user), messages, receipts, presence: presenceRows(room.id) });
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
  if (requiresPolicyAcceptance(req) && !policyAcceptedAt(user)) { socket.write('HTTP/1.1 428 Precondition Required\r\n\r\n'); return socket.destroy(); }
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
