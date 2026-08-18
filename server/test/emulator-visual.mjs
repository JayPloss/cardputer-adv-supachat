import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const base = process.env.SUPACHAT_EMULATOR_URL || 'http://127.0.0.1:8877/emulator/';
const output = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../emulator/screens');
fs.mkdirSync(output, { recursive: true });
const browser = await chromium.launch({ headless: true });
const page = await browser.newPage({ viewport: { width: 1366, height: 768 } });
await page.goto(base);
const screen = page.locator('#screen');
assert.equal(await screen.getAttribute('width'), '240');
assert.equal(await screen.getAttribute('height'), '135');
const shot = name => screen.screenshot({ path: path.join(output, `${name}.png`) });
const state = async () => JSON.parse(await page.locator('#state').textContent());

const firmware = fs.readFileSync(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../firmware/src/main.cpp'), 'utf8');
const version = firmware.match(/kFirmwareVersion\[\] = "(v\d+\.\d{2})"/)[1];
async function bootShot(asset, name) {
  await page.evaluate(async ({asset, version}) => {
    const canvas = document.querySelector('#screen'), context = canvas.getContext('2d');
    const image = new Image(); image.src = asset; await image.decode();
    context.drawImage(image, 0, 0, 240, 135);
    context.fillStyle = '#d6d6d6'; context.font = '8px "Courier New",monospace'; context.textBaseline = 'top';
    context.fillText(version, 4, 125);
  }, {asset, version});
  await shot(name);
}
await bootShot('/firmware/assets/boot/supachat-splash-albie-240x135.png', 'boot-albie');
await bootShot('/firmware/assets/boot/supachat-splash-juju-240x135.png', 'boot-juju');
await page.reload();

await shot('chat');
await page.evaluate(()=>{window.supachatState.completeDuel(true);window.renderSupachat();});
assert.equal((await state()).duel.status,'complete');await shot('chat-duel-won');
await page.getByRole('button',{name:'REAR · MENU'}).click();await page.getByRole('button',{name:'↓ .'}).click();await page.getByRole('button',{name:'↓ .'}).click();await page.getByRole('button',{name:'ENTER'}).click();await shot('duel-victory');
await page.getByRole('button',{name:'ENTER'}).click();assert.equal((await state()).duel,null);
await page.reload();
await page.getByRole('button', { name: 'SHIFT + / = ?' }).click();
assert.equal((await state()).draft, '?');
assert.equal((await state()).screen, 'chat');
assert.ok((await state()).tones > 0);
await page.getByRole('button', { name: 'REAR · MENU' }).click();
await shot('menu');

async function openMenu(pageIndex,index) {
  await page.reload();
  await page.getByRole('button', { name: 'REAR · MENU' }).click();
  if(pageIndex)await page.getByRole('button',{name:'PAGE ▶'}).click();
  for (let i = 0; i < index; i++) await page.getByRole('button', { name: '↓ .' }).click();
  await page.getByRole('button', { name: 'ENTER' }).click();
}

await openMenu(0,2);await shot('duel-select');await page.getByRole('button',{name:'↑ ;'}).click();assert.equal((await state()).duelSelection,0);await shot('duel-protego-selected');await page.getByRole('button',{name:'ENTER'}).click();await shot('duel-locked');
await page.evaluate(()=>{window.supachatState.receiveChallenge('Papa');window.renderSupachat();});await shot('duel-incoming');
await openMenu(0,1); await shot('rooms'); await page.getByRole('button', { name: '↓ .' }).click(); await page.getByRole('button', { name: 'ENTER' }).click(); assert.equal((await state()).currentRoom, 'K-BUDS');
await openMenu(0,3); await shot('walkie-ready');
const ptt = page.getByRole('button', { name: 'HOLD SPACE · PTT' });
const box = await ptt.boundingBox(); assert.ok(box);
await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2); await page.mouse.down();
await page.waitForTimeout(250); await shot('walkie-recording'); await page.mouse.up();
assert.equal((await state()).clipReady, true); await page.getByRole('button', { name: 'ENTER' }).click();
assert.equal((await state()).replayAudible, true); await shot('walkie-replay');
await openMenu(1,1); await shot('volume');
await openMenu(1,2); await shot('networks'); await page.getByRole('button', { name: 'ENTER' }).click(); await shot('network-password');
await openMenu(1,3); await shot('status');
await page.reload(); await page.getByLabel('Sync fault injection').selectOption('io');
assert.equal((await state()).network, 'SYNC IO -1'); await shot('chat-sync-io');
await browser.close();
console.log('supachat_emulator_visual=PASS');
