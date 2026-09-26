// Exercise the actual shell DOM without booting the emulator; game replay is separate.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import puppeteer from 'puppeteer';
const html = fs.readFileSync('../../emu/wasm/shell.html', 'utf8').replace(/\{\{\{\s*SCRIPT\s*\}\}\}/g, '');
const browser = await puppeteer.launch({executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium', headless: true, args: ['--no-sandbox']});
try {
  for (const width of [1280, 390]) {
    const page = await browser.newPage();
    await page.setViewport({width, height: 844});
    const errors: string[] = [], consoleLines: string[] = [];
    page.on('pageerror', error => errors.push(String(error)));
    page.on('console', message => consoleLines.push(message.text()));
    await page.setContent(html);
    const result = await page.evaluate(async () => {
      const g = window as any;
      const panel = document.getElementById('log-panel') as HTMLDetailsElement;
      const log = document.getElementById('log')!;
      const frame = () => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      if (panel.open) throw new Error('Log should start collapsed');
      for (let i = 0; i < 5000; i++) g.Module.print(`line-${i}`);
      g._flushLog(); await frame();
      if (log.textContent !== '') throw new Error('Collapsed log mutated DOM');
      panel.open = true; await frame();
      if (!log.textContent!.includes('line-4999') || log.textContent!.includes('line-0\n')) throw new Error('Wrong retained tail');
      if (log.textContent!.length > 32800 || log.textContent!.split('\n').length > 201) throw new Error('Unbounded DOM');
      g.Module.printErr('<img src=x onerror=alert(1)>'); await frame();
      if (!log.textContent!.includes('[ERR] <img') || log.querySelector('img')) throw new Error('Error/text rendering');
      g.Module.print('x'.repeat(100000)); g._flushLog(); await frame();
      if (log.textContent!.length > 32800) throw new Error('Long line exceeded cap');
      panel.open = false; await frame(); const old = log.textContent;
      g.Module.printErr('closed-error'); await frame();
      if (log.textContent !== old) throw new Error('Closed error changed DOM');
      panel.open = true; await frame();
      if (!log.textContent!.includes('closed-error')) throw new Error('Reopen lost error');
      return {width: innerWidth, logWidth: log.getBoundingClientRect().width, boundedBytes: log.textContent!.length};
    });
    assert.equal(errors.length, 0, errors.join('\n'));
    const all = consoleLines.join('\n');
    assert(all.includes('line-0\n') && all.includes('line-4999') && all.includes('closed-error'));
    assert(all.includes('x'.repeat(100000)));
    assert(result.logWidth <= width);
    console.log('PASS log UI', JSON.stringify(result));
    await page.close();
  }
} finally { await browser.close(); }
