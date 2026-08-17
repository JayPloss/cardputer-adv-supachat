import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

const html = readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
const app = readFileSync(new URL('../web/app.js', import.meta.url), 'utf8');

test('web admin zone is restricted to admin sessions and creates invitations', () => {
  assert.match(html, /id="admin-open"[^>]+hidden/);
  assert.match(html, /id="admin-zone"/);
  assert.match(html, /id="invite-form"/);
  assert.match(app, /currentUser\?\.role !== 'admin'/);
  assert.match(app, /api\('api\/admin\/invitations'/);
  assert.match(app, /navigator\.share/);
});
