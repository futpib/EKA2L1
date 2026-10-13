// The running device keeps WasmFS memory storage. Browser transactions save
// changed files; the manifest switches only after every new file is durable.
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
    async load() {
        this.manifest = await this.request(this.database.transaction('devices').objectStore('devices').get(this.id)) || null;
        if (!this.manifest) { await this.collectOrphans(); return; }
        if (this.manifest.version !== 1 || !Array.isArray(this.manifest.entries)
                || this.manifest.entries.some(entry => !this.validPath(entry.path)
                    || typeof entry.directory !== 'boolean' || !Number.isSafeInteger(entry.mode)
                    || !Number.isSafeInteger(entry.modified) || !Number.isSafeInteger(entry.size) || entry.size < 0
                    || (!entry.directory && typeof entry.revision !== 'string'))
                || new Set(this.manifest.entries.map(entry => entry.path)).size !== this.manifest.entries.length)
            throw new Error('Invalid saved device');
        this.installed = {...this.manifest.installed};
        this.filesystem.mkdirTree('/data');
        for (const entry of this.manifest.entries) if (entry.directory) this.filesystem.mkdirTree(entry.path);
        const range = IDBKeyRange.bound([this.id], [this.id, []]);
        const expected = new Map(this.manifest.entries.filter(entry => !entry.directory).map(entry => [entry.path, entry]));
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
        const live = new Set((this.manifest?.entries || []).filter(entry => !entry.directory)
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
                if (!entry.changed && old.has(entry.path)) {
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
                    && JSON.stringify(this.installed) === JSON.stringify(this.manifest.installed)
                    && entries.every(entry => old.has(entry.path) && entry.mode === old.get(entry.path).mode
                        && entry.directory === old.get(entry.path).directory && entry.modified === old.get(entry.path).modified)) {
                accepted = true; this.notify('Device saved'); return;
            }
            const manifest = {id:this.id, version:1, entries, installed:{...this.installed}, saved:Date.now()};
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
