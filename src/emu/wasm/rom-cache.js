// Optional, derived ARM-to-WASM code. Guest state and installed files are not
// restored; missing or incompatible caches keep on-demand compilation.
globalThis.EkaRomCache = {
    async prepare(expected) {
        if (!expected) return;
        const state = globalThis.ekaRomCacheStatus = {status:'loading', bytes:expected.bytes};
        const path = '/tmp/rom-cache.bin';
        try {
            const build = new URL(globalThis.ekaAssetUrls['/eka2l1.wasm'], location.href).searchParams.get('v');
            if (build !== expected.buildSha256 || typeof Module._eka2l1_rom_cache_configure !== 'function')
                throw new Error('ROM cache targets a different emulator build');
            const response = await globalThis.ekaDownload(expected.url);
            if (!response.ok) throw new Error('ROM cache download failed: HTTP ' + response.status);
            const bytes = new Uint8Array(await response.arrayBuffer());
            if (bytes.length !== expected.bytes) throw new Error('ROM cache size mismatch');
            const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)))
                .map(byte => byte.toString(16).padStart(2, '0')).join('');
            if (hash !== expected.sha256) throw new Error('ROM cache SHA-256 mismatch');
            FS.close(FS.open(path, 'w'));
            FS.writeFile(path, bytes);
            if (Module.ccall('eka2l1_rom_cache_configure', 'number', ['string','string'], [path,build]) !== 0)
                throw new Error('ROM cache configuration failed');
            state.status = 'staged';
        } catch (error) {
            try { FS.unlink(path); } catch {}
            state.status = 'fallback'; state.reason = String(error);
            console.info('ROM cache unavailable; compiling on demand', state.reason);
        }
    }
};
