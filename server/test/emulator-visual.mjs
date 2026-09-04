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
const shot = async name => {
  const dataUrl = await screen.evaluate(canvas => canvas.toDataURL('image/png'));
  fs.writeFileSync(path.join(output, `${name}.png`), Buffer.from(dataUrl.split(',')[1], 'base64'));
};
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
await bootShot('/firmware/assets/boot/supachat-splash-papa-240x135.png', 'boot-papa');
await bootShot('/firmware/assets/boot/supachat-splash-mama-240x135.png', 'boot-mama');
await page.reload();

await shot('chat');
await page.getByRole('button', { name: 'SHIFT + / = ?' }).click();
assert.equal((await state()).draft, '?');
assert.equal((await state()).screen, 'chat');
assert.ok((await state()).tones > 0);
await page.getByRole('button', { name: 'REAR · MENU' }).click();
await shot('menu');

async function openMenu(index) {
  await page.reload();
  await page.getByRole('button', { name: 'REAR · MENU' }).click();
  for (let pageIndex = 0; pageIndex < Math.floor(index / 6); pageIndex++) await page.locator('[data-key="RIGHT"]').click();
  for (let i = 0; i < index % 6; i++) await page.getByRole('button', { name: '↓ .' }).click();
  await page.getByRole('button', { name: 'ENTER' }).click();
}

await openMenu(1); await shot('rooms'); await page.getByRole('button', { name: '↓ .' }).click(); await page.getByRole('button', { name: 'ENTER' }).click(); assert.equal((await state()).currentRoom, 'K-BUDS');
await openMenu(3); await shot('voice-messages-ready');
const ptt = page.getByRole('button', { name: 'HOLD SPACE · PTT' });
const box = await ptt.boundingBox(); assert.ok(box);
await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2); await page.mouse.down();
await page.waitForTimeout(250); await shot('voice-message-recording'); await page.mouse.up();
assert.equal((await state()).clipReady, true); await page.getByRole('button', { name: 'ENTER' }).click();
assert.equal((await state()).replayAudible, true); await shot('voice-message-replay');
await openMenu(4); await shot('walkie-ready'); await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2); await page.mouse.down(); await page.waitForTimeout(250); await shot('walkie-transmitting'); await page.mouse.up(); assert.equal((await state()).clipPending,false);
await openMenu(5); await shot('volume');
await openMenu(6); await shot('language'); await page.locator('[data-key="RIGHT"]').click(); assert.equal((await state()).languageOverride, 'en'); assert.equal((await state()).screen,'language');
await openMenu(7); await shot('networks'); await page.getByRole('button', { name: 'ENTER' }).click(); await shot('network-password');
await page.reload(); await page.getByRole('button', { name: 'REAR · MENU' }).click(); await page.locator('[data-key="RIGHT"]').click(); await shot('menu-page-2');
await openMenu(8); assert.equal((await state()).localOnly,true); assert.equal((await state()).ssid,''); assert.equal((await state()).network,'ESPNOW LOCAL'); await shot('menu-local-only');
await openMenu(9); await shot('charging-confirm'); await page.getByRole('button', { name: 'ENTER' }).click(); assert.equal((await state()).charging,true); await shot('charging-active');
await openMenu(10); await shot('status'); await page.locator('[data-key="RIGHT"]').click(); assert.equal((await state()).statusPage,1); await shot('status-diagnostics-empty');
await page.reload(); await page.getByLabel('Sync fault injection').selectOption('bad-data'); await page.getByRole('button', { name: 'REAR · MENU' }).click(); await page.locator('[data-key="RIGHT"]').click(); for(let i=0;i<4;i++)await page.getByRole('button', { name: '↓ .' }).click(); await page.getByRole('button', { name: 'ENTER' }).click(); await page.locator('[data-key="RIGHT"]').click(); assert.equal((await state()).lastSyncError,'BAD JSON'); await shot('status-diagnostics-bad-json');
await openMenu(11); await shot('changelog-current'); await page.locator('[data-key="RIGHT"]').click(); assert.equal((await state()).changelogSelection,1); await shot('changelog-previous'); await page.getByRole('button', { name: '↓ .' }).click(); assert.equal((await state()).changelogLineOffset,1); await shot('changelog-scrolled');
await openMenu(13); await shot('fox-finding-select'); await page.getByRole('button', { name: 'ENTER' }).click(); assert.equal((await state()).foxState,'requesting'); await shot('fox-finding-requesting');
await page.evaluate(()=>{window.__supachatState.foxAck();window.__supachatState.foxPlaceResult();window.__supachatDraw()}); assert.equal((await state()).foxState,'guiding'); await shot('fox-finding-guiding');
await page.evaluate(()=>{window.__supachatState.foxLoseSignal();window.__supachatDraw()}); await shot('fox-finding-signal-lost');
await openMenu(13); await page.evaluate(()=>{window.__supachatState.offerPack('Papa','Juju',77);window.__supachatDraw()}); await shot('fox-finding-pack-invite'); await page.getByRole('button', { name: 'ENTER' }).click(); await page.evaluate(()=>{window.__supachatState.foxPlaceResult();window.__supachatState.packObservation('Albie',{rssi:-67,similarity:72,confidence:2});window.__supachatState.packObservation('Papa',{rssi:-52,similarity:80,confidence:3});window.__supachatState.packObservation('Naomie',{rssi:-60,similarity:91,confidence:2});window.__supachatDraw()}); assert.equal((await state()).foxJoinedPack,true); await shot('fox-finding-pack-guiding');
await page.reload(); await page.getByLabel('Sync fault injection').selectOption('io');
assert.equal((await state()).network, 'SYNC IO -1'); await shot('chat-sync-io');
await page.goto(`${base}?language=fr`);
await page.getByRole('button', { name: 'SHIFT + / = ?' }).click();
const entry = page.getByLabel('Printable text');
await entry.fill("'a'e"); await entry.dispatchEvent('input');
assert.equal((await state()).draft, 'éàè');
await shot('chat-french-accents');
await browser.close();
console.log('supachat_emulator_visual=PASS');
