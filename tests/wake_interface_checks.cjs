const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { pathToFileURL } = require('node:url');
const assert = require('node:assert/strict');
const { chromium } = require('playwright');

(async () => {
  const browser = await chromium.launch({ channel: 'msedge', headless: true });
  const evidence = process.env.HIBERNATION_TEST_ARTIFACTS || path.join(os.tmpdir(), 'hibernation-wake-tests');
  fs.mkdirSync(evidence, { recursive: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1120, height: 752 } });
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    page.on('console', m => { if (['error', 'warning'].includes(m.type())) errors.push(m.text()); });
    await page.addInitScript(() => {
      window.sent = [];
      window.chrome = { webview: {
        postMessage: m => window.sent.push(m),
        addEventListener: (_, handler) => window.receive = handler
      } };
    });
    const appUrl = pathToFileURL(path.resolve('src/web/app.html')).href;
    await page.goto(appUrl);
    assert.equal(page.url(), appUrl);
    assert((await page.title()).includes('Hibernation'));
    const defaults = { version: '1.0.0', monitors: 1, context: 'Desktop', hotkeysOk: true };
    for (const m of fs.readFileSync('src/core.h', 'utf8').matchAll(/(?:int|bool)\s+(\w+)\s*=\s*(\d+|true|false|'[A-Z]')\s*;/g)) {
      defaults[m[1]] = m[2] === 'true' ? true : m[2] === 'false' ? false : m[2][0] === "'" ? m[2].charCodeAt(1) : Number(m[2]);
    }
    await page.evaluate(s => window.receive({data:JSON.stringify(s)}), defaults);
    await page.locator('[data-tab="behavior"]').click();
    const distance = page.locator('#behRun .row').filter({ hasText: 'Mouse wake distance' });
    assert((await distance.innerText()).includes('resets after 200 ms'));
    const range = distance.locator('input');
    await range.fill('73');
    assert(await page.evaluate(() => sent.includes('set:wakeMouseThreshold:73')));
    const protection = page.locator('#behRun .row').filter({ hasText: 'Accidental wake protection' }).locator('.sw');
    await protection.click();
    assert(await distance.evaluate(r => r.classList.contains('off')));
    await protection.click();
    assert(!(await distance.evaluate(r => r.classList.contains('off'))));
    await page.locator('#behRun').scrollIntoViewIfNeeded();
    await page.screenshot({ path: path.join(evidence, 'wake-settings.png') });

    await page.clock.install();
    const lockUrl = pathToFileURL(path.resolve('src/web/lock.html')).href;
    await page.goto(lockUrl);
    assert.equal(page.url(), lockUrl);
    assert((await page.title()).length > 0);
    await page.evaluate(() => {
      receive({ data: 'theme:{"palette":3,"nativeWake":true,"wakeKeyboard":false,"length":6}' });
      receive({ data: 'reveal' });
    });
    const away = () => page.locator('#card').evaluate(c => c.inert && c.classList.contains('away'));
    const noise = () => page.evaluate(() => document.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, clientX: 401, clientY: 300, movementX: 1 })));
    assert(!(await away()));
    await page.keyboard.type('abc');
    assert.equal(await page.locator('#pw').inputValue(), 'abc', 'typing still works when the prompt is open');
    for (let i = 0; i < 6; i++) { await page.clock.fastForward(1000); await noise(); }
    assert(await away(), 'unfiltered browser motion cannot keep the password open');
    assert.equal(await page.locator('#pw').inputValue(), '', 'idle clears partial passwords');
    assert(await page.evaluate(() => sent.includes('idle')), 'hiding asks the native filter to discard the previous gesture');
    await page.keyboard.press('a');
    assert(await away(), 'keyboard wake disabled also applies to the hidden card');
    await page.mouse.click(450, 450);
    assert(await away(), 'DOM mouse events cannot bypass the native filter');
    assert((await page.locator('#wake').innerText()).includes('Move the mouse or click'));
    await page.screenshot({ path: path.join(evidence, 'wake-password-idle.png'), animations: 'disabled' });
    await page.evaluate(() => receive({ data: 'wake' }));
    assert(!(await away()), 'native accepted input wakes the prompt');
    await page.keyboard.type('ab');
    await page.evaluate(() => receive({ data: 'wake' }));
    assert.equal(await page.locator('#pw').inputValue(), 'ab', 'accepted mouse motion never erases ongoing password input');

    await page.clock.fastForward(5100);
    assert(await away());
    await page.evaluate(() => window.sent = []);
    for (let i = 0; i < 10; i++) { await noise(); await page.clock.fastForward(1000); }
    assert(await page.evaluate(() => sent.includes('rest')), 'noise cannot postpone the return to the animation');
    await page.evaluate(() => {
      receive({ data: 'theme:{"palette":3,"nativeWake":true,"wakeKeyboard":true,"length":6}' });
      receive({ data: 'reveal' });
    });
    await page.clock.fastForward(5100);
    await page.keyboard.press('a');
    assert(!(await away()), 'enabled keyboard wake returns the hidden prompt');
    await page.evaluate(() => receive({ data: 'reveal' }));
    await page.keyboard.type('abcdef');
    assert.deepEqual(await page.evaluate(() => sent.filter(m => m.startsWith('try:'))), ['try:abcdef']);
    assert.deepEqual(errors, [], 'no browser errors or warnings');
    console.log('PASS: settings, native-filter-only mouse wake, keyboard option, idle cleanup, rest timeout and password submission.');
    console.log('Evidence: ' + evidence);
  } finally { await browser.close(); }
})().catch(e => { console.error(e); process.exitCode = 1; });
