import assert from 'node:assert/strict';
import { chromium } from 'playwright';

const base = process.env.SUPACHAT_STATIC_URL || 'http://127.0.0.1:8877/server/web/';
const browser = await chromium.launch({ headless: true });
const page = await browser.newPage({ viewport: { width: 900, height: 620 } });
await page.addInitScript(() => {
  window.notificationOscillators = [];
  class AudioParam { setValueAtTime() {} exponentialRampToValueAtTime() {} }
  class AudioNode { connect() { return this; } disconnect() {} }
  class AudioContext {
    constructor() { this.state = 'running'; this.currentTime = 0; this.destination = {}; }
    resume() { return Promise.resolve(); }
    createOscillator() { const node = new AudioNode(); node.frequency = { value:0 }; node.start = () => window.notificationOscillators.push(node.frequency.value); node.stop = () => {}; return node; }
    createGain() { const node = new AudioNode(); node.gain = new AudioParam(); return node; }
  }
  window.AudioContext = AudioContext;
  window.fetch = async (url) => ({ ok:true, status:200, json:async () => String(url).includes('api/session') ? {user:{id:'papa',display_name:'Papa'},rooms:[{id:'wolfpack',name:'Wolfpack'}],policy:{accepted_at:Date.now()}} : String(url).includes('presence') ? {presence:[]} : {messages:[]}, arrayBuffer:async () => new ArrayBuffer(0) });
  window.EventSource = class {
    constructor() { this.handlers = {}; window.testEvents = this; }
    addEventListener(name, handler) { (this.handlers[name] ||= []).push(handler); }
    emit(name, data) { for (const handler of this.handlers[name] || []) handler({data:JSON.stringify(data)}); }
  };
  window.WebSocket = class { static OPEN = 1; constructor() { this.readyState = 0; } send() {} };
});
await page.goto(base);
await page.locator('#message').click();
await page.evaluate(() => window.testEvents.emit('message', {id:77, conversation_id:'wolfpack', author_id:'albie', author_name:'Albie', type:'text', body:'Ping', created_at:Date.now()}));
await page.waitForTimeout(50);
assert.deepEqual(await page.evaluate(() => window.notificationOscillators), [587, 784, 659]);
assert.equal(await page.locator('#sound-toggle').getAttribute('aria-pressed'), 'true');
await page.locator('#language-select').selectOption('fr');
await page.waitForTimeout(25);
assert.equal(await page.locator('#sound-toggle').textContent(), 'Son : activé');
assert.equal(await page.locator('#message').getAttribute('placeholder'), 'Écrivez un message…');
assert.equal(await page.evaluate(() => localStorage.getItem('supachat-language')), 'fr');
await browser.close();
console.log('supachat_web_notification=PASS');
