import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { chromium } from 'playwright';

const webRoot = join(import.meta.dirname, '..', 'web');
const server = createServer((request, response) => {
  const pathname = new URL(request.url, 'http://127.0.0.1').pathname;
  const name = pathname === '/' ? 'index.html' : pathname.slice(1);
  const mime = name.endsWith('.css') ? 'text/css' : name.endsWith('.js') ? 'text/javascript' : name.endsWith('.png') ? 'image/png' : 'text/html';
  try { response.writeHead(200, {'content-type':mime}); response.end(readFileSync(join(webRoot, name))); }
  catch { response.writeHead(404); response.end(); }
});
await new Promise((resolve) => server.listen(18876, '127.0.0.1', resolve));

const browser = await chromium.launch({headless:true});
try {
  for (const viewport of [{width:390,height:844},{width:1440,height:900}]) {
    const page = await browser.newPage({viewport});
    await page.route('**/api/**', async (route) => {
      const url = route.request().url();
      const rooms = [{id:'family',name:'Family',member_count:3,members:[{id:'papa',display_name:'Papa',kind:'web'}]},{id:'k-buds',name:'K-BUDS',member_count:1,members:[{id:'papa',display_name:'Papa',kind:'web'}]}];
      const groups = [{id:'household',name:'Household',member_count:1,members:[{id:'papa',display_name:'Papa',kind:'web'}]}];
      const json = url.includes('/session') ? {user:{id:'papa',display_name:'Papa',role:'admin'},rooms:rooms.map(({id,name})=>({id,name})),policy:{version:'2026-08-21',accepted_at:Date.now()}} : url.includes('/admin/compliance') ? {reports:[],deletion_requests:[]} : url.includes('/admin/user-groups') ? {groups,users:groups[0].members} : url.includes('/admin/rooms') ? {rooms,users:rooms[0].members} : url.includes('/messages') ? {messages:[]} : url.includes('/presence') ? {presence:[]} : {url:'https://auth.supachat.net/if/flow/supachat-invitation-enrollment/?itoken=test',qr_data_url:'data:image/png;base64,iVBORw0KGgo=',room_ids:['family'],user_group_id:null};
      await route.fulfill({status:url.includes('/invitations') ? 201 : 200,contentType:'application/json',body:JSON.stringify(json)});
    });
    await page.addInitScript(() => {
      window.EventSource = class { addEventListener() {} };
      window.WebSocket = class { static OPEN = 1; constructor() { this.readyState = 0; } };
    });
    await page.goto('http://127.0.0.1:18876/?welcome=1', {waitUntil:'networkidle'});
    await page.locator('#welcome-zone[open]').waitFor();
    const welcomeGeometry = await page.evaluate(() => {
      const dialog = document.querySelector('#welcome-zone').getBoundingClientRect();
      const card = document.querySelector('#welcome-zone [data-vi-container="true"]');
      return {bodyWidth:document.body.scrollWidth,viewportWidth:document.documentElement.clientWidth,viewportHeight:document.documentElement.clientHeight,dialog:dialog.toJSON(),cardScrollHeight:card.scrollHeight,cardClientHeight:card.clientHeight};
    });
    assert.ok(welcomeGeometry.bodyWidth <= welcomeGeometry.viewportWidth + 1, `welcome causes horizontal overflow at ${viewport.width}px`);
    assert.ok(welcomeGeometry.dialog.x >= 0 && welcomeGeometry.dialog.x + welcomeGeometry.dialog.width <= viewport.width, `welcome escapes viewport width at ${viewport.width}px`);
    assert.ok(welcomeGeometry.dialog.y >= 0 && welcomeGeometry.dialog.y + welcomeGeometry.dialog.height <= viewport.height, `welcome escapes viewport height at ${viewport.width}px`);
    assert.ok(welcomeGeometry.cardScrollHeight <= welcomeGeometry.cardClientHeight + 1, `welcome content overflows its card at ${viewport.width}px`);
    await page.screenshot({path:join(process.env.TEMP || '.',`supachat-welcome-${viewport.width}x${viewport.height}.png`),fullPage:true});
    await page.locator('#welcome-close').click();
    assert.equal(new URL(page.url()).searchParams.has('welcome'), false);
    await page.locator('#admin-open').click();
    await page.locator('#invite-name').fill('Aunt Sarah');
    await page.locator('#invite-username').fill('sarah');
    await page.locator('#invite-generate').click();
    await page.locator('#invite-result:not([hidden])').waitFor();
    assert.match(await page.locator('#invite-link').getAttribute('href'), /^https:\/\/auth\.supachat\.net\/if\/flow\//);
    const geometry = await page.evaluate(() => {
      const dialog = document.querySelector('#admin-zone').getBoundingClientRect();
      const card = document.querySelector('#invite-form');
      return {bodyWidth:document.body.scrollWidth,viewportWidth:document.documentElement.clientWidth,dialog:dialog.toJSON(),cardScrollHeight:card.scrollHeight,cardClientHeight:card.clientHeight};
    });
    assert.ok(geometry.bodyWidth <= geometry.viewportWidth + 1, `horizontal overflow at ${viewport.width}px`);
    assert.ok(geometry.dialog.x >= 0 && geometry.dialog.x + geometry.dialog.width <= viewport.width, `dialog escapes viewport at ${viewport.width}px`);
    assert.ok(geometry.dialog.y >= 0 && geometry.dialog.y + geometry.dialog.height <= viewport.height, `dialog escapes viewport height at ${viewport.width}px`);
    assert.ok(geometry.cardScrollHeight <= geometry.cardClientHeight + 1, `admin controls overflow their card at ${viewport.width}px`);
    await page.screenshot({path:join(process.env.TEMP || '.',`supachat-admin-${viewport.width}x${viewport.height}.png`),fullPage:true});
    await page.close();
  }
} finally { await browser.close(); await new Promise((resolve) => server.close(resolve)); }
console.log('supachat_web_admin_layout=PASS');
