import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const html = readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
const app = readFileSync(new URL('../web/app.js', import.meta.url), 'utf8');

test('web admin zone is restricted to admin sessions and creates invitations', () => {
  assert.match(html, /id="admin-open"[^>]+hidden/);
  assert.match(html, /id="admin-zone"/);
  assert.match(html, /id="invite-form"/);
  assert.match(html, /id="invite-room"/);
  assert.match(app, /currentUser\?\.role !== 'admin'/);
  assert.match(app, /api\('api\/admin\/invitations'/);
  assert.match(app, /navigator\.share/);
  assert.match(app, /Join our SUPACHAT \$\{roomName\} room/);
  assert.doesNotMatch(app, /Join our SUPACHAT Family room/);
  assert.match(app, /if \(currentRoom !== requestedRoom\) return/);
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

test('web messaging exposes room-safe duel and standard message controls',()=>{
  assert.match(html,/id="duel-zone"/);assert.match(html,/id="replying"/);
  assert.match(app,/api\/duels\/current\?room=/);assert.match(app,/reply_to_id:replyTarget/);
  assert.match(app,/api\/typing/);assert.match(app,/\/reactions/);assert.match(app,/message_update/);
  assert.match(app,/room\.unread_count/);
});
