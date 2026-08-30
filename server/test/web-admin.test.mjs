import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const html = readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
const app = readFileSync(new URL('../web/app.js', import.meta.url), 'utf8');
const caddyPatch = readFileSync(new URL('../deploy/patch-caddy.py', import.meta.url), 'utf8');
const authentikConfig = readFileSync(new URL('../deploy/configure-authentik.py', import.meta.url), 'utf8');

test('web admin zone is restricted to admin sessions and creates invitations', () => {
  assert.match(html, /id="admin-open"[^>]+hidden/);
  assert.match(html, /id="admin-zone"/);
  assert.match(html, /id="invite-form"/);
  assert.match(html, /name="group_ids"/);
  assert.match(html, /id="new-room-group"/);
  assert.match(html, /id="group-default-language"/);
  assert.match(html, /id="invite-qr"/);
  assert.match(html, /id="manage-user-group"/);
  assert.match(html, /id="compliance-queue"/);
  assert.match(app, /currentUser\?\.role !== 'admin'/);
  assert.match(app, /api\('api\/admin\/invitations'/);
  assert.match(app, /api\('api\/admin\/rooms'/);
  assert.match(app, /api\('api\/admin\/groups'/);
  assert.match(app, /api\('api\/admin\/compliance'/);
  assert.match(app, /navigator\.share/);
  assert.match(app, /Join these SUPACHAT groups/);
  assert.doesNotMatch(app, /Join our SUPACHAT Family room/);
  assert.match(app, /if \(currentRoom !== requestedRoom\) return/);
});

test('web client requires current policy acceptance and exposes safety links', () => {
  assert.match(html, /id="safety-open"/);
  assert.match(html, /id="policy-zone"/);
  assert.match(html, /href="\/privacy"/);
  assert.match(html, /href="\/terms"/);
  assert.match(html, /href="\/delete-account"/);
  assert.match(app, /api\('api\/policy\/accept'/);
  assert.match(app, /policyRequired = !session\.policy\?\.accepted_at/);
});

test('policy and deletion routes bypass portal login without exposing chat', () => {
  assert.match(caddyPatch, /@supachat_public path \/healthz \/privacy[\s\S]*\/api\/account\/deletion\/public/);
  assert.match(caddyPatch, /reverse_proxy @supachat_public 127\.0\.0\.1:8094/);
  assert.doesNotMatch(caddyPatch, /@supachat_public[^\n]*(?:\/api\/messages|\/api\/session|\/api\/admin)/);
  assert.ok(caddyPatch.indexOf('reverse_proxy @supachat_public') < caddyPatch.indexOf('forward_auth @supachat_browser'));
});

test('newly enrolled users receive a one-time welcome modal', () => {
  assert.match(html, /id="welcome-zone"/);
  assert.match(html, /id="welcome-title"/);
  assert.match(html, /Messages/);
  assert.match(html, /Sound/);
  assert.match(html, /Voice/);
  assert.match(html, /Presence/);
  assert.match(app, /URLSearchParams\(location\.search\)\.get\('welcome'\) === '1'/);
  assert.match(app, /supachat-welcomed:/);
  assert.match(app, /welcomeZone\.showModal\(\)/);
});

test('Authentik is SupaChat branded and application groups stay out of identity-provider groups', () => {
  assert.match(authentikConfig, /content: "Powered by Authentik"/);
  assert.match(authentikConfig, /branding_logo.*https:\/\/supachat\.net\/supachat-logo\.png/);
  assert.doesNotMatch(authentikConfig, /get_or_create\(name="SupaChat (?:Family|K-BUDS|Wolfpack)"/);
  assert.match(authentikConfig, /user\.groups\.add\(group\)/);
});
