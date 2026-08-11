import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.resolve(here, '../web');
const appCss = fs.readFileSync(path.join(web, 'app.css'), 'utf8');
const controlsCss = fs.readFileSync(path.join(web, 'controls.css'), 'utf8');
const output = path.resolve(here, '../../emulator/screens/identity-colors.png');

const browser = await chromium.launch({ headless: true });
const page = await browser.newPage({ viewport: { width: 900, height: 620 } });
await page.setContent(`
  <style>${appCss}\n${controlsCss}</style>
  <main class="app-shell">
    <div class="presence">
      <div class="person author-albie"><span class="dot online"></span><strong>Albie</strong></div>
      <div class="person author-juju"><span class="dot online"></span><strong>Julien</strong></div>
      <div class="person author-papa"><span class="dot online"></span><strong>Papa</strong></div>
    </div>
    <section class="messages">
      <article class="message author-albie"><div class="message-meta"><strong>Albie</strong></div><div class="bubble">Light blue, checking in.</div></article>
      <article class="message author-juju"><div class="message-meta"><strong>Julien</strong></div><div class="bubble">Orange, checking in.</div></article>
      <article class="message mine author-papa"><div class="message-meta"><strong>Papa</strong></div><div class="bubble">Green, checking in.</div></article>
    </section>
  </main>`);

const expected = {
  albie: 'rgb(125, 211, 252)',
  juju: 'rgb(255, 173, 92)',
  papa: 'rgb(167, 240, 112)',
};
for (const [id, colour] of Object.entries(expected)) {
  const actual = await page.locator(`.person.author-${id} strong`).evaluate(el => getComputedStyle(el).color);
  assert.equal(actual, colour, `${id} identity colour`);
}
await page.screenshot({ path: output, fullPage: true });
await browser.close();
console.log('supachat_identity_colours=PASS');
