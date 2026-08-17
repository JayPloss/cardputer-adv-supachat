import assert from 'node:assert/strict';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { chromium } from 'playwright-core';

const password = process.env.SUPACHAT_PAPA_PASSWORD;
const baseUrl = process.env.SUPACHAT_PORTAL_URL || 'https://supachat.net/';
if (!password) throw new Error('SUPACHAT_PAPA_PASSWORD is required');

const executablePath = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const output = mkdtempSync(join(tmpdir(), 'supachat-layout-'));
const browser = await chromium.launch({ executablePath, headless: true });

function overlaps(a, b) {
  return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
}

try {
  for (const viewport of [{ width: 390, height: 844 }, { width: 1440, height: 900 }]) {
    const context = await browser.newContext({ viewport });
    const page = await context.newPage(); const errors = [];
    page.on('pageerror', (error) => errors.push(String(error)));
    await page.goto(`${baseUrl}login`, { waitUntil: 'networkidle' });
    await page.locator('input[name=password]').fill(password);
    await Promise.all([page.waitForURL(baseUrl), page.locator('button[type=submit]').click()]);
    await page.waitForSelector('#messages'); await page.waitForTimeout(1000);

    const geometry = await page.evaluate(() => ({
      bodyWidth: document.body.scrollWidth, viewportWidth: document.documentElement.clientWidth,
      shell: document.querySelector('.app-shell').getBoundingClientRect().toJSON(),
      header: document.querySelector('.topbar').getBoundingClientRect().toJSON(),
      logo: document.querySelector('.brand-logo').getBoundingClientRect().toJSON(),
      presence: document.querySelector('#presence').getBoundingClientRect().toJSON(),
      chat: document.querySelector('.chat-panel').getBoundingClientRect().toJSON(),
      voice: document.querySelector('.voice-controls').getBoundingClientRect().toJSON(),
      clip: document.querySelector('#voice-clip').getBoundingClientRect().toJSON(),
      ptt: document.querySelector('#ptt').getBoundingClientRect().toJSON(),
      composer: document.querySelector('#composer').getBoundingClientRect().toJSON(),
      messageCount: document.querySelectorAll('#messages .message').length,
      messageHeight: document.querySelector('#messages').clientHeight,
    }));
    console.log(`portal geometry ${viewport.width}x${viewport.height}: ${JSON.stringify(geometry)}`);
    assert.ok(geometry.bodyWidth <= geometry.viewportWidth + 1, `horizontal overflow at ${viewport.width}px`);
    for (const [name, rect] of Object.entries(geometry).filter(([, value]) => value && typeof value === 'object' && 'x' in value)) {
      assert.ok(rect.x >= -1 && rect.x + rect.width <= viewport.width + 1, `${name} escapes viewport at ${viewport.width}px`);
    }
    assert.equal(overlaps(geometry.clip, geometry.ptt), false, `voice buttons overlap at ${viewport.width}px`);
    assert.equal(overlaps(geometry.voice, geometry.composer), false, `voice controls overlap composer at ${viewport.width}px`);
    assert.ok(geometry.messageCount > 0, 'deployed conversation history is blank');
    assert.ok(geometry.messageHeight >= 250, `conversation history is too short at ${viewport.width}px`);
    if (viewport.width > 760) {
      assert.equal(overlaps(geometry.chat, geometry.voice), false, 'desktop chat and walkie columns overlap');
      assert.ok(geometry.voice.x > geometry.chat.x, 'walkie is not in the second desktop column');
      assert.equal(overlaps(geometry.logo, geometry.presence), false, 'logo and presence collide in compact header');
      assert.ok(geometry.logo.y < geometry.header.y + geometry.header.height && geometry.presence.y < geometry.header.y + geometry.header.height, 'logo and presence do not share header row');
    }
    assert.deepEqual(errors, []);
    const screenshot = join(output, `portal-${viewport.width}x${viewport.height}.png`);
    await page.screenshot({ path: screenshot, fullPage: true });
    console.log(`portal layout passed: ${viewport.width}x${viewport.height} screenshot=${screenshot}`);
    await context.close();
  }
} finally {
  await browser.close();
}
