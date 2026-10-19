// Experimental session cache for immutable generated ROM modules. The bytes
// live in OPFS; no WebAssembly.Module objects are retained by this script.
globalThis.EkaRecycleCache = {
    async prepare(module) {
        if (globalThis.ekaRecycleCacheExperiment !== true
                && new URLSearchParams(location.search).get('recycleCache') !== '1') return;
        const state = globalThis.ekaRecycleCacheStatus = {status:'waiting'};
        while (!module._eka2l1_opfs_status()) await new Promise(resolve => setTimeout(resolve, 10));
        const root = '/opfs/eka2l1-aot-recycle';
        try {
            FS.stat('/opfs');
            FS.mkdirTree(root);
            const previous = sessionStorage.getItem('ekaRecycleCachePath');
            if (previous?.startsWith(root + '/')) {
                try { FS.unlink(previous); } catch (error) { if (error.errno !== 44) throw error; }
            }
            sessionStorage.removeItem('ekaRecycleCachePath');
            const expired = Date.now() - 7 * 24 * 60 * 60 * 1000;
            for (const name of FS.readdir(root)) {
                if (!/^[0-9a-f-]{36}\.bin(?:\.gc)?$/.test(name)) continue;
                const path = root + '/' + name;
                if (+FS.stat(path).mtime < expired) FS.unlink(path);
            }
            const path = root + '/' + crypto.randomUUID() + '.bin';
            const limitMiB = Number(globalThis.ekaRecycleCacheMaxMiB || 4);
            if (!Number.isInteger(limitMiB) || limitMiB < 4 || limitMiB > 128)
                throw new Error('Invalid ROM recycle cache limit');
            const functionsPerWorker = Number(globalThis.ekaRecycleCacheFunctionsPerWorker || 20000);
            if (!Number.isInteger(functionsPerWorker) || functionsPerWorker < 1000 || functionsPerWorker > 100000)
                throw new Error('Invalid CPU recycle interval');
            if (module.ccall('eka2l1_recycle_cache_configure', 'number', ['string','number'],
                    [path, limitMiB * 1024 * 1024]) !== 0)
                throw new Error('Could not configure ROM recycle cache');
            sessionStorage.setItem('ekaRecycleCachePath', path);
            state.status = 'active'; state.path = path; state.limitMiB = limitMiB;
            state.functionsPerWorker = functionsPerWorker;
            addEventListener('pagehide', () => {
                try { FS.unlink(path); } catch {}
            }, {once:true});
        } catch (error) {
            state.status = 'fallback'; state.reason = String(error);
            console.warn('[recycle-cache]', state.reason);
        }
    }
};
