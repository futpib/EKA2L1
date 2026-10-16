// The running device keeps hot WasmFS files in memory. The installed ROM copy
// can live in OPFS after installation and before the game launches.
// Browser transactions save changed files and their manifest atomically.
globalThis.EkaDeviceDisk = class {
    static async open(id, filesystem, module, notify = () => {}) {
        if (!globalThis.indexedDB || !navigator.locks) throw new Error('Persistent device storage is unavailable in this browser');
        let ready, release, lockError, database;
        const acquired = new Promise(resolve => ready = resolve);
        const hold = new Promise(resolve => release = resolve);
        const lock = navigator.locks.request('eka2l1-device-' + id, {ifAvailable:true}, async value => {
            ready(!!value);
            if (value) await hold;
        }).catch(error => { lockError = error; ready(false); });
        if (!await acquired) throw lockError || new Error('This device is open in another tab. Close that tab first.');
        try {
            database = await new Promise((resolve, reject) => {
                const request = indexedDB.open('eka2l1-device-disks', 1);
                request.onupgradeneeded = () => {
                    request.result.createObjectStore('devices', {keyPath:'id'});
                    request.result.createObjectStore('files', {keyPath:['device','revision','path']});
                };
                request.onerror = () => reject(request.error);
                request.onblocked = () => reject(new Error('Close older emulator tabs to open device storage'));
                request.onsuccess = () => resolve(request.result);
            });
            database.onversionchange = () => database.close();
            const disk = new this(id, database, filesystem, module, notify, release);
            disk.lock = lock;
            await disk.load();
            navigator.storage?.persist?.().catch(() => {});
            return disk;
        } catch (error) { database?.close(); release(); throw error; }
    }
    constructor(id, database, filesystem, module, notify, release) {
        Object.assign(this, {id, database, filesystem, module, notify, release});
        if (!filesystem.ekaDeviceTracking) {
            const write = filesystem.writeFile.bind(filesystem);
            filesystem.writeFile = (path, ...args) => {
                module.ccall('eka2l1_disk_modified', null, ['string'], [path]);
                const result = write(path, ...args);
                module.ccall('eka2l1_disk_modified', null, ['string'], [path]);
                return result;
            };
            filesystem.ekaDeviceTracking = true;
        }
        this.manifest = null;
        this.installed = {};
        this.active = null;
        this.timer = null;
        this.canSave = false;
        this.romOffloadRoot = null;
        this.firmwareOffloadRoot = null;
        this.firmwareOffloadDone = false;
    }
    request(request) {
        return new Promise((resolve, reject) => {
            request.onsuccess = () => resolve(request.result);
            request.onerror = () => reject(request.error);
        });
    }
    transaction(stores, action) {
        return new Promise((resolve, reject) => {
            const tx = this.database.transaction(stores, 'readwrite', {durability:'strict'});
            tx.oncomplete = resolve;
            tx.onabort = () => reject(tx.error || new Error('Device save was aborted'));
            tx.onerror = () => {};
            try { action(tx); } catch (error) { tx.abort(); reject(error); }
        });
    }
    validPath(path) {
        return typeof path === 'string' && path.startsWith('/data/') && !path.includes('\\')
            && !path.includes('\0') && path.split('/').slice(1).every(part => part && part !== '.' && part !== '..');
    }
    validOffloadedPath(path) {
        return /^\/data\/roms\/[a-z0-9_-]+\/SYM\.ROM$/i.test(path)
            || (this.validPath(path) && /^\/data\/drives\/z\/[a-z0-9_-]+\/.+/i.test(path));
    }
    offloadTarget(path) {
        if (path.startsWith('/data/drives/z/'))
            return this.firmwareOffloadRoot + '/' + path.slice('/data/drives/z/'.length);
        return this.romOffloadRoot + '/' + path.split('/')[3] + '/SYM.ROM';
    }
    exists(path) {
        try { this.filesystem.stat(path); return true; }
        catch (error) { if (error.errno === 44) return false; throw error; }
    }
    async prepareRomOffload() {
        if (!this.exists('/opfs')) return;
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(this.id)));
        const namespace = Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join('');
        this.romOffloadRoot = '/opfs/eka2l1-roms/' + namespace;
        this.firmwareOffloadRoot = '/opfs/eka2l1-firmware/' + namespace;
    }
    collectOffloadedOrphans() {
        if (!this.romOffloadRoot || !this.exists(this.romOffloadRoot)) return;
        const live = new Set((this.manifest?.entries || []).filter(entry => entry.offloaded
                && entry.path.startsWith('/data/roms/'))
            .map(entry => this.offloadTarget(entry.path)));
        for (const name of this.filesystem.readdir(this.romOffloadRoot)) {
            if (!/^[a-z0-9_-]+$/i.test(name)) continue;
            const target = this.romOffloadRoot + '/' + name + '/SYM.ROM';
            if (!live.has(target) && this.exists(target) && this.filesystem.unlink(target) !== 0)
                console.warn('[device-disk] Could not remove unreferenced OPFS ROM:', target);
        }
    }
    offloadFile(path, info, chunk) {
        const target = this.offloadTarget(path);
        const modified = this.module.ccall('eka2l1_disk_modified_ms', 'number', ['string'], [path]);
        if (modified < 0) throw new Error('Could not read readonly file metadata');
        this.filesystem.mkdirTree(target.slice(0, target.lastIndexOf('/')));
        let input;
        try {
            const output = this.filesystem.open(target, 'w'); this.filesystem.close(output);
            input = this.filesystem.open(path, 'r');
            for (let offset = 0; offset < info.size;) {
                const count = this.filesystem.read(input, chunk, 0, Math.min(chunk.length, info.size - offset), offset);
                if (count <= 0) throw new Error('Short readonly file copy');
                this.filesystem.writeFile(target, chunk.subarray(0, count));
                offset += count;
            }
            if (this.filesystem.stat(target).size !== info.size) throw new Error('Short OPFS file copy');
            if (this.module.ccall('eka2l1_disk_set_metadata', 'number', ['string','number','number'],
                    [target, info.mode, modified]))
                throw new Error('Could not preserve read-only file metadata');
        } catch (error) {
            try { this.filesystem.unlink(target); } catch {}
            throw error;
        } finally { if (input) this.filesystem.close(input); }
        const backup = path + '.eka-opfs-pending';
        this.filesystem.rename(path, backup);
        let linked = false;
        try {
            this.filesystem.symlink(target, path);
            linked = true;
            this.filesystem.unlink(backup);
        } catch (error) {
            if (linked) this.filesystem.unlink(path);
            this.filesystem.rename(backup, path);
            try { this.filesystem.unlink(target); } catch {}
            throw error;
        }
        return info.size;
    }
    async offloadRom() {
        if (!this.romOffloadRoot) return 0;
        let moved = 0;
        let chunk;
        if (this.exists('/data/roms')) for (const name of this.filesystem.readdir('/data/roms')) {
            const path = '/data/roms/' + name + '/SYM.ROM';
            if (!this.validOffloadedPath(path) || !this.exists(path)) continue;
            const info = this.filesystem.lstat(path);
            if ((info.mode & 0o170000) === 0o120000 || info.size < 4 * 1024 * 1024) continue;
            this.module.setStatus?.('Moving installed ROM to browser storage...');
            moved += this.offloadFile(path, info, chunk ??= new Uint8Array(4 * 1024 * 1024));
        }
        if (!this.firmwareOffloadDone && this.exists('/data/drives/z')) {
            const stack = ['/data/drives/z'];
            while (stack.length) {
                const directory = stack.pop();
                for (const name of this.filesystem.readdir(directory)) {
                    if (name === '.' || name === '..') continue;
                    const path = directory + '/' + name;
                    const info = this.filesystem.lstat(path);
                    if ((info.mode & 0o170000) === 0o040000) { stack.push(path); continue; }
                    if (!this.validOffloadedPath(path) || (info.mode & 0o170000) !== 0o100000
                            || info.size < 64 * 1024) continue;
                    this.module.setStatus?.('Moving read-only firmware to browser storage...');
                    moved += this.offloadFile(path, info, chunk ??= new Uint8Array(4 * 1024 * 1024));
                }
            }
            this.firmwareOffloadDone = true;
        }
        if (moved) console.log('[device-disk] Moved ' + moved + ' read-only bytes to OPFS');
        return moved;
    }
    async load() {
        await this.prepareRomOffload();
        this.manifest = await this.request(this.database.transaction('devices').objectStore('devices').get(this.id)) || null;
        if (!this.manifest) { this.collectOffloadedOrphans(); await this.collectOrphans(); return; }
        if (this.manifest.version !== 1 || !Array.isArray(this.manifest.entries)
                || (this.manifest.firmwareOffloadDone !== undefined
                    && typeof this.manifest.firmwareOffloadDone !== 'boolean')
                || this.manifest.entries.some(entry => !this.validPath(entry.path)
                    || typeof entry.directory !== 'boolean' || !Number.isSafeInteger(entry.mode)
                    || !Number.isSafeInteger(entry.modified) || !Number.isSafeInteger(entry.size) || entry.size < 0
                    || (entry.offloaded && (entry.directory || !this.validOffloadedPath(entry.path)))
                    || (!entry.directory && !entry.offloaded && typeof entry.revision !== 'string'))
                || new Set(this.manifest.entries.map(entry => entry.path)).size !== this.manifest.entries.length)
            throw new Error('Invalid saved device');
        this.collectOffloadedOrphans();
        this.installed = {...this.manifest.installed};
        this.firmwareOffloadDone = !!this.manifest.firmwareOffloadDone;
        this.filesystem.mkdirTree('/data');
        for (const entry of this.manifest.entries) if (entry.directory) this.filesystem.mkdirTree(entry.path);
        for (const entry of this.manifest.entries) if (entry.offloaded) {
            if (!this.romOffloadRoot || !this.exists(this.offloadTarget(entry.path)))
                throw new Error('Saved read-only OPFS file is unavailable');
            this.filesystem.symlink(this.offloadTarget(entry.path), entry.path);
        }
        const range = IDBKeyRange.bound([this.id], [this.id, []]);
        const expected = new Map(this.manifest.entries.filter(entry => !entry.directory && !entry.offloaded)
            .map(entry => [entry.path, entry]));
        let restored = 0;
        await new Promise((resolve, reject) => {
            const tx = this.database.transaction('files');
            tx.oncomplete = resolve;
            tx.onabort = () => reject(tx.error || new Error('Device restore aborted'));
            const request = tx.objectStore('files').openCursor(range);
            request.onerror = () => reject(request.error);
            request.onsuccess = () => {
                const cursor = request.result;
                if (!cursor) return;
                try {
                    const file = cursor.value;
                    const entry = expected.get(file.path);
                    if (!entry || file.revision !== entry.revision) { cursor.continue(); return; }
                    if (file.bytes.length !== entry.size)
                        throw new Error('Saved device file is missing or invalid');
                    const stream = this.filesystem.open(file.path, 'w'); this.filesystem.close(stream);
                    if (file.bytes.length) this.filesystem.writeFile(file.path, file.bytes);
                    ++restored;
                    cursor.continue();
                } catch (error) { tx.abort(); reject(error); }
            };
        });
        if (restored !== expected.size) throw new Error('Saved device files are missing');
        for (const entry of [...this.manifest.entries].reverse())
            if (this.module.ccall('eka2l1_disk_set_metadata', 'number', ['string','number','number'], [entry.path, entry.mode, entry.modified]))
                throw new Error('Could not restore device metadata');
        await this.scan(true);
        await this.collectOrphans();
        this.notify('Device restored');
        console.log('[device-disk] Restored ' + this.manifest.entries.filter(entry => !entry.directory).length + ' files');
    }
    async collectOrphans() {
        const live = new Set((this.manifest?.entries || []).filter(entry => !entry.directory && !entry.offloaded)
            .map(entry => JSON.stringify([this.id, entry.revision, entry.path])));
        const keys = await this.request(this.database.transaction('files').objectStore('files')
            .getAllKeys(IDBKeyRange.bound([this.id], [this.id, []])));
        const stale = keys.filter(key => !live.has(JSON.stringify(key)));
        if (stale.length) await this.transaction(['files'], tx => {
            for (const key of stale) tx.objectStore('files').delete(key);
        });
    }
    sameFile(entry) {
        return this.module.ccall('eka2l1_disk_file_matches', 'number', ['string'], [entry.path]) === 1;
    }
    readFile(entry) {
        // The SDK's FS.readFile retains its native allocation. FS.read frees
        // each temporary chunk, keeping large ROM saves inside the heap limit.
        const bytes = new Uint8Array(entry.size), stream = this.filesystem.open(entry.path, 'r');
        try {
            for (let offset = 0; offset < bytes.length;) {
                const count = this.filesystem.read(stream, bytes, offset, Math.min(4 * 1024 * 1024, bytes.length - offset), offset);
                if (count <= 0) throw new Error('Short device file read');
                offset += count;
            }
        } finally { this.filesystem.close(stream); }
        return bytes;
    }
    async scan(baseline = false) {
        if (this.module.ccall('eka2l1_disk_scan', 'number', [], []) !== 0) throw new Error('Device scan already running');
        let accepted = false;
        const staged = [];
        try {
            let value;
            while (!(value = this.module.ccall('eka2l1_disk_scan_result', 'string', [], [])))
                await new Promise(resolve => setTimeout(resolve, 20));
            const batch = JSON.parse(value);
            if (batch.error) throw new Error(batch.error);
            if (baseline) { accepted = true; return; }
            const old = new Map((this.manifest?.entries || []).map(entry => [entry.path, entry]));
            const revision = crypto.randomUUID(), entries = [];
            let writes = [], buffered = 0;
            const flush = async () => {
                if (!writes.length) return;
                await this.transaction(['files'], tx => {
                    for (const file of writes) tx.objectStore('files').put(file);
                });
                for (const file of writes) staged.push([this.id, revision, file.path]);
                writes = []; buffered = 0;
            };
            for (const entry of batch.entries) {
                if (!this.validPath(entry.path)) throw new Error('Invalid device path');
                if (entry.directory) { entries.push(entry); continue; }
                if (entry.offloaded) {
                    if (!this.validOffloadedPath(entry.path)) throw new Error('Invalid offloaded path');
                    entries.push({...entry, offloaded:true}); continue;
                }
                if (!entry.changed && old.has(entry.path) && !old.get(entry.path).offloaded) {
                    entries.push({...entry, revision:old.get(entry.path).revision}); continue;
                }
                if (!this.sameFile(entry)) throw new Error('Device file changed while saving; retrying');
                const bytes = this.readFile(entry);
                if (!this.sameFile(entry)) throw new Error('Device file changed while saving; retrying');
                writes.push({device:this.id, revision, path:entry.path, bytes});
                buffered += bytes.length;
                if (buffered >= 4 * 1024 * 1024 || writes.length >= 128) await flush();
                entries.push({...entry, revision});
            }
            await flush();
            if (!staged.length && entries.length === old.size && this.manifest
                    && this.firmwareOffloadDone === !!this.manifest.firmwareOffloadDone
                    && JSON.stringify(this.installed) === JSON.stringify(this.manifest.installed)
                    && entries.every(entry => old.has(entry.path) && entry.mode === old.get(entry.path).mode
                        && entry.directory === old.get(entry.path).directory && entry.modified === old.get(entry.path).modified
                        && entry.size === old.get(entry.path).size
                        && !!entry.offloaded === !!old.get(entry.path).offloaded)) {
                accepted = true; this.notify('Device saved'); return;
            }
            const manifest = {id:this.id, version:1, entries, installed:{...this.installed},
                firmwareOffloadDone:this.firmwareOffloadDone, saved:Date.now()};
            await this.transaction(['devices'], tx => tx.objectStore('devices').put(manifest));
            this.manifest = manifest;
            accepted = true;
            await this.collectOrphans();
            this.notify('Device saved');
            console.log('[device-disk] Saved ' + staged.length + ' changed files');
        } catch (error) {
            if (!accepted && staged.length) await this.transaction(['files'], tx => {
                for (const key of staged) tx.objectStore('files').delete(key);
            }).catch(() => {});
            throw error;
        } finally { this.module.ccall('eka2l1_disk_ack', null, ['number'], [accepted ? 1 : 0]); }
    }
    async save(force = false) {
        while (this.active) {
            if (!force) return this.active;
            await this.active.catch(() => {});
        }
        this.notify('Saving device…');
        this.active = this.scan().catch(error => {
            this.notify('Device not saved: ' + error.message);
            throw error;
        }).finally(() => this.active = null);
        return this.active;
    }
    start() {
        this.canSave = true;
        this.timer ??= setInterval(() => {
            if (this.canSave) this.save().catch(error => console.warn('[device-disk]', error.message));
        }, 10000);
        return this.save();
    }
    async stop() {
        clearInterval(this.timer); this.timer = null;
        clearInterval(this.installTimer); this.installTimer = null;
        const saveAllowed = this.canSave;
        this.canSave = false;
        try {
            if (this.active) await this.active.catch(() => {});
            if (this.module._eka2l1_graphics_ready?.() > 0)
                await this.module.ccall('eka2l1_shutdown', null, [], [], {async:true});
            if (saveAllowed) await this.save(true);
            this.release(); this.database.close();
        } catch (error) { this.canSave = saveAllowed; throw error; }
    }
};
