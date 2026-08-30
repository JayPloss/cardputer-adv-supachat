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
      <div class="person author-theo"><span class="dot online"></span><strong>Théo</strong></div>
      <div class="person author-josee"><span class="dot online"></span><strong>Josée</strong></div>
      <div class="person author-emmanuelle"><span class="dot online"></span><strong>Emmanuelle</strong></div>
      <div class="person author-andrew"><span class="dot online"></span><strong>Andrew</strong></div>
      <div class="person author-naomie"><span class="dot online"></span><strong>Naomie</strong></div>
    </div>
    <section class="messages">
      <article class="message author-albie"><div class="bubble"><strong class="message-sender">Albie</strong><span class="message-separator">: </span><span class="message-body">Light blue, checking in.</span></div></article>
      <article class="message author-juju"><div class="bubble"><strong class="message-sender">Julien</strong><span class="message-separator">: </span><span class="message-body">Orange, checking in.</span></div></article>
      <article class="message mine author-papa"><div class="bubble"><strong class="message-sender">Papa</strong><span class="message-separator">: </span><span class="message-body">Green, checking in.</span></div></article>
    </section>
  </main>`);

const expected = {
  albie: 'rgb(125, 211, 252)',
  juju: 'rgb(255, 173, 92)',
  papa: 'rgb(167, 240, 112)',
  theo: 'rgb(196, 167, 255)',
  josee: 'rgb(255, 143, 184)',
  emmanuelle: 'rgb(96, 225, 224)',
  andrew: 'rgb(244, 211, 94)',
  naomie: 'rgb(255, 107, 107)',
};
for (const [id, colour] of Object.entries(expected)) {
  const actual = await page.locator(`.person.author-${id} strong`).evaluate(el => getComputedStyle(el).color);
  assert.equal(actual, colour, `${id} identity colour`);
}
assert.equal(await page.locator('.message.author-albie .message-sender').evaluate(el => getComputedStyle(el).color), expected.albie);
assert.equal(await page.locator('.message.author-albie .message-body').evaluate(el => getComputedStyle(el).color), 'rgb(246, 240, 220)');
await page.screenshot({ path: output, fullPage: true });
await browser.close();
console.log('supachat_identity_colours=PASS');
