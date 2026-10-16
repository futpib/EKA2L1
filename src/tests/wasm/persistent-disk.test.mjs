import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import fs from 'node:fs';
import http from 'node:http';
import puppeteer from 'puppeteer';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = process.env.EKA2L1_WASM_BUILD_DIR || path.resolve(here, '../../../build-wasm/src/tests/aot');
// The filesystem fixture has no emulator launcher or compiler policy.
const server = http.createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    const file = path.join(build, path.basename(new URL(request.url, 'http://localhost').pathname));
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) { response.writeHead(404).end(); return; }
    response.setHeader('Content-Type', file.endsWith('.wasm') ? 'application/wasm' : file.endsWith('.js') ? 'application/javascript' : 'text/html');
    fs.createReadStream(file).pipe(response);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const {port} = server.address();
const browserName = process.env.EKA_TEST_BROWSER || 'chrome';
const browser = await puppeteer.launch({browser:browserName,
    executablePath:browserName === 'firefox' ? '/usr/bin/firefox' : '/usr/bin/chromium',headless:true,
    args:browserName === 'firefox' ? [] : ['--no-sandbox','--disable-dev-shm-usage']});
const errors = [], logs = [], url = `http://127.0.0.1:${port}/test_device_disk.html`;
const page = await browser.newPage();
page.on('pageerror', error => errors.push(String(error)));
page.on('console', message => { logs.push(message.text()); if (logs.length > 20) logs.shift(); });
const load = async () => {
    await page.goto(url);
    try { await page.waitForFunction(() => Module.calledRun && Module._eka2l1_opfs_status?.() === 1,{timeout:90000}); }
    catch (error) { throw new Error('Fixture startup timed out: ' + errors.join(' | ') + ' ' + logs.join(' | '),{cause:error}); }
};
const open = id => page.evaluate(async id => {
    window.disk = await EkaDeviceDisk.open(id, FS, Module);
}, id);
try {
    await load(); await open('device-A');
    const original = await page.evaluate(async () => {
        FS.mkdirTree('/data/c');
        for (const [name, value] of [['save','original save'],['empty',''],['remove','delete me']])
            if (Module.ccall('test_disk_write','number',['string','string'],['/data/c/'+name,value])) throw Error('native write failed');
        if (Module.ccall('test_disk_large','number',['string'],['/data/c/large'])) throw Error('native large write failed');
        if (Module.ccall('test_disk_buffered','number',[],[])) throw Error('native buffered write failed');
        disk.installed.game = true;
        await disk.save();
        return {checksum:Module.ccall('test_disk_checksum','number',['string'],['/data/c/large']),
            modified:Module.ccall('test_disk_modified','number',['string'],['/data/c/save'])};
    });
    const second = await browser.newPage();
    await second.goto(url); await second.waitForFunction(() => Module.calledRun);
    const locked = await second.evaluate(async () => {
        try { await EkaDeviceDisk.open('device-A', FS, Module); return false; }
        catch (error) { return /another tab/.test(error.message); }
    });
    assert.equal(locked, true); await second.close();
    await load(); await open('device-A');
    assert.deepEqual(await page.evaluate(() => ({save:FS.readFile('/data/c/save',{encoding:'utf8'}),
        empty:FS.stat('/data/c/empty').size,buffered:FS.readFile('/data/c/buffered',{encoding:'utf8'}),installed:disk.installed.game,
        checksum:Module.ccall('test_disk_checksum','number',['string'],['/data/c/large'])})),
        {save:'original save',empty:0,buffered:'buffered native write',installed:true,checksum:original.checksum});
    assert.equal(await page.evaluate(() => Module.ccall('test_disk_modified','number',['string'],['/data/c/save'])),original.modified);
    await page.evaluate(async () => {
        if (Module.ccall('test_disk_write','number',['string','string'],['/data/c/save','modified save'])) throw Error('same-size write failed');
        if (Module.ccall('test_disk_rename','number',['string','string'],['/data/c/save','/data/c/renamed'])) throw Error('rename failed');
        if (Module.ccall('test_disk_remove','number',['string'],['/data/c/remove'])) throw Error('unlink failed');
        if (Module.ccall('test_disk_truncate','number',['string','number'],['/data/c/large',3])) throw Error('truncate failed');
        await disk.save();
    });
    await load(); await open('device-A');
    assert.deepEqual(await page.evaluate(() => ({value:FS.readFile('/data/c/renamed',{encoding:'utf8'}),
        removed:!FS.analyzePath('/data/c/remove').exists,old:!FS.analyzePath('/data/c/save').exists,size:FS.stat('/data/c/large').size})),
        {value:'modified save',removed:true,old:true,size:3});
    await page.evaluate(async () => {
        await disk.save(); await new Promise(resolve => setTimeout(resolve, 10)); await disk.save();
        if (Module.ccall('test_disk_preserve_time','number',['string','string'],['/data/c/renamed','MODIFIED save'])) throw Error('mtime preservation failed');
        await disk.save();
    });
    await load(); await open('device-A');
    assert.equal(await page.evaluate(() => FS.readFile('/data/c/renamed',{encoding:'utf8'})), 'MODIFIED save');
    await page.evaluate(async () => {
        await disk.save(); await new Promise(resolve => setTimeout(resolve, 10)); await disk.save();
        const stream = FS.open('/data/c/renamed', 'w'); FS.close(stream);
        FS.writeFile('/data/c/renamed', new TextEncoder().encode('JS saved text'));
        await disk.save();
    });
    await load(); await open('device-A');
    assert.equal(await page.evaluate(() => FS.readFile('/data/c/renamed',{encoding:'utf8'})), 'JS saved text');
    const rom = await page.evaluate(async () => {
        FS.mkdirTree('/data/roms/rm-409');
        const path = '/data/roms/rm-409/SYM.ROM';
        if (Module.ccall('test_disk_large','number',['string'],[path])) throw Error('ROM fixture write failed');
        const checksum = Module.ccall('test_disk_checksum','number',['string'],[path]);
        await disk.save();
        const moved = await disk.offloadRom();
        await disk.save();
        const entry = disk.manifest.entries.find(value => value.path === path);
        const keys = await disk.request(disk.database.transaction('files').objectStore('files').getAllKeys());
        return {checksum,moved,offloaded:!!entry.offloaded,
            linked:(FS.lstat(path).mode & 0o170000) === 0o120000,
            stored:keys.some(key => key[2] === path)};
    });
    assert.equal(rom.moved > 0, browserName === 'chrome');
    if (rom.moved) {
        assert.equal(rom.moved, 5 * 1024 * 1024 + 31);
        assert.deepEqual({offloaded:rom.offloaded,linked:rom.linked,stored:rom.stored},
            {offloaded:true,linked:true,stored:false});
    } else {
        assert.deepEqual({offloaded:rom.offloaded,linked:rom.linked,stored:rom.stored},
            {offloaded:false,linked:false,stored:true});
    }
    await load(); await open('device-A');
    assert.deepEqual(await page.evaluate(() => {
        const path = '/data/roms/rm-409/SYM.ROM';
        return {checksum:Module.ccall('test_disk_checksum','number',['string'],[path]),
            linked:(FS.lstat(path).mode & 0o170000) === 0o120000,
            offloaded:!!disk.manifest.entries.find(value => value.path === path).offloaded};
    }),{checksum:rom.checksum,linked:rom.linked,offloaded:rom.offloaded});
    if (rom.moved) {
        const rollback = await page.evaluate(async () => {
            FS.mkdirTree('/data/roms/rm-500');
            const path = '/data/roms/rm-500/SYM.ROM';
            if (Module.ccall('test_disk_large','number',['string'],[path])) throw Error('rollback fixture write failed');
            const checksum = Module.ccall('test_disk_checksum','number',['string'],[path]);
            const symlink = FS.symlink;
            FS.symlink = () => { throw Error('Injected symlink failure'); };
            try {
                await disk.offloadRom();
                throw Error('ROM offload should have failed');
            } catch (error) {
                if (!/Injected symlink failure/.test(error.message)) throw error;
            } finally { FS.symlink = symlink; }
            const target = disk.offloadTarget(path);
            const preserved = {regular:(FS.lstat(path).mode & 0o170000) === 0o100000,
                checksum:Module.ccall('test_disk_checksum','number',['string'],[path]) === checksum};
            FS.unlink(path);
            return {target,...preserved};
        });
        assert.deepEqual({regular:rollback.regular,checksum:rollback.checksum},
            {regular:true,checksum:true});
        await load();
        await open('device-A');
        assert.equal(await page.evaluate(target => FS.analyzePath(target).exists,rollback.target),false);
    }
    if (browserName === 'chrome') {
        const firmware = await page.evaluate(async () => {
            const path = '/data/drives/z/rm-409/resource/large.rsc';
            FS.mkdirTree('/data/drives/z/rm-409/resource');
            if (Module.ccall('test_disk_large','number',['string'],[path])) throw Error('firmware write failed');
            const checksum = Module.ccall('test_disk_checksum','number',['string'],[path]);
            const modified = Module.ccall('test_disk_modified','number',['string'],[path]);
            const mode = FS.stat(path).mode & 0o777;
            disk.firmwareOffloadEnabled = true;
            const moved = await disk.offloadRom();
            await disk.save();
            return {path, checksum, modified, mode, moved,
                sameModified:Module.ccall('test_disk_modified','number',['string'],[path]) === modified,
                sameMode:(FS.stat(path).mode & 0o777) === mode,
                linked:(FS.lstat(path).mode & 0o170000) === 0o120000,
                offloaded:disk.manifest.entries.find(entry => entry.path === path)?.offloaded};
        });
        assert.equal(firmware.moved, 5 * 1024 * 1024 + 31);
        assert.deepEqual({linked:firmware.linked,offloaded:firmware.offloaded,
            sameModified:firmware.sameModified,sameMode:firmware.sameMode},
            {linked:true,offloaded:true,sameModified:true,sameMode:true});
        await load(); await open('device-A');
        assert.deepEqual(await page.evaluate(path => ({
            checksum:Module.ccall('test_disk_checksum','number',['string'],[path]),
            linked:(FS.lstat(path).mode & 0o170000) === 0o120000,
            mode:FS.stat(path).mode & 0o777,
            modified:Module.ccall('test_disk_modified','number',['string'],[path]),
            complete:disk.manifest.firmwareOffloadDone
        }),firmware.path),{checksum:firmware.checksum,linked:true,
            mode:firmware.mode,modified:firmware.modified,complete:true});
    }
    await page.evaluate(async () => {
        Module.ccall('test_disk_write','number',['string','string'],['/data/c/renamed','must not commit']);
        const original = disk.transaction.bind(disk);
        disk.transaction = (stores, callback) => stores.includes('devices')
            ? Promise.reject(new Error('Injected manifest failure')) : original(stores, callback);
        try { await disk.save(); throw Error('Save should have failed'); }
        catch (error) { if (!/Injected/.test(error.message)) throw error; }
    });
    await load(); await open('device-A');
    assert.equal(await page.evaluate(() => FS.readFile('/data/c/renamed',{encoding:'utf8'})), 'JS saved text');
    await load(); await open('device-B');
    assert.equal(await page.evaluate(() => disk.manifest === null && !FS.analyzePath('/data/c/renamed').exists), true);
    assert.deepEqual(errors, []);
    console.log('PASS native writes and libc buffers, reload, timestamps, empty/large files, ROM and firmware offload/reload, JS writes, rename, deletion, truncation, atomic failure, profile isolation and tab exclusion; OPFS=' + (rom.moved > 0));
} finally { await browser.close(); await new Promise(resolve => server.close(resolve)); }
