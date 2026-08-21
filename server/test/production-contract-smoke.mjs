import assert from 'node:assert/strict';

const base = (process.env.SUPACHAT_SMOKE_BASE || 'http://127.0.0.1:8094').replace(/\/$/, '');
const headers = {
  'x-forwarded-host': process.env.SUPACHAT_SMOKE_PORTAL_HOST || 'supachat.net',
  'x-authentik-uid': process.env.SUPACHAT_SMOKE_UID || 'production-smoke-papa',
  'x-authentik-username': process.env.SUPACHAT_SMOKE_USERNAME || 'papa',
  'x-authentik-name': 'Papa',
};

const getJson = async (path) => {
  const response = await fetch(`${base}${path}`, {headers});
  assert.equal(response.status, 200, `${path} returned ${response.status}`);
  return response.json();
};

for (const path of ['/healthz','/privacy','/terms','/delete-account']) {
  const response = await fetch(`${base}${path}`);
  assert.equal(response.status, 200, `${path} returned ${response.status}`);
}

const session = await getJson('/api/session');
assert.equal(session.user.id, 'papa');
assert.equal(session.user.role, 'admin');
assert.equal(session.policy.version, '2026-08-21');
assert.deepEqual(session.rooms.map((room) => room.id).sort(), ['family','k-buds']);

const roomCounts = {};
for (const roomId of ['family','k-buds']) {
  const history = await getJson(`/api/messages?room=${encodeURIComponent(roomId)}&limit=100`);
  assert.ok(history.messages.every((message) => message.conversation_id === roomId), `${roomId} returned another room's history`);
  roomCounts[roomId] = history.messages.length;
}

const groups = await getJson('/api/admin/groups');
assert.deepEqual(groups.groups.map((group) => group.id).sort(), ['family','k-buds']);
const compliance = await getJson('/api/admin/compliance');
assert.ok(Array.isArray(compliance.reports));
assert.ok(Array.isArray(compliance.deletion_requests));
assert.ok(Array.isArray((await getJson('/api/moderation/blocks')).blocked));

const invalidInvite = await fetch(`${base}/api/admin/invitations`, {
  method:'POST', headers:{...headers,origin:'https://supachat.net','content-type':'application/json'}, body:'{}',
});
assert.equal(invalidInvite.status, 400);

console.log(`production_contract_smoke=PASS family_history=${roomCounts.family} kbuds_history=${roomCounts['k-buds']} policy=${session.policy.version}`);
