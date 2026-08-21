import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {chromium} from 'playwright';

const webRoot = join(import.meta.dirname, '..', 'web');
const types = {'.css':'text/css','.png':'image/png','.html':'text/html'};
const server = createServer((request, response) => {
  const name = request.url === '/' ? 'privacy.html' : request.url.replace(/^\//, '').split('?')[0];
  const extension = name.slice(name.lastIndexOf('.'));
  try { response.writeHead(200, {'content-type':types[extension] || 'text/html'}); response.end(readFileSync(join(webRoot, name))); }
  catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(8878, '127.0.0.1', resolve));
const browser = await chromium.launch({headless:true});
for (const viewport of [{width:360,height:740},{width:412,height:915},{width:1366,height:768}]) {
  const page = await browser.newPage({viewport});
  for (const name of ['privacy.html','terms.html','delete-account.html']) {
    await page.goto(`http://127.0.0.1:8878/${name}`);
    const geometry = await page.locator('[data-vi-container="true"]').evaluate((element) => {
      const rect=element.getBoundingClientRect();
      return {left:rect.left,right:rect.right,width:rect.width,documentWidth:document.documentElement.scrollWidth,viewportWidth:innerWidth};
    });
    assert.ok(geometry.left >= 0, `${name} starts outside ${viewport.width}px viewport`);
    assert.ok(geometry.right <= geometry.viewportWidth, `${name} exceeds ${viewport.width}px viewport`);
    assert.equal(geometry.documentWidth, geometry.viewportWidth, `${name} has horizontal overflow at ${viewport.width}px`);
  }
  await page.close();
}
await browser.close(); server.close();
console.log('supachat_policy_layout=PASS');
