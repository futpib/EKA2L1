// Installed files, not a running-emulator checkpoint. Each launch restores a
// fresh writable filesystem before C++ creates the device/package managers.
globalThis.EkaInstalledImage = {
    async restore(response, expected, filesystem) {
        if (!response.ok) throw new Error('Installed image download failed: HTTP ' + response.status);
        const reader = response.body.pipeThrough(new DecompressionStream('gzip')).getReader();
        let pending = new Uint8Array(), position = 0;
        async function read(size) {
            const bytes = new Uint8Array(size);
            let written = 0;
            while (written < size) {
                if (position === pending.length) {
                    const next = await reader.read();
                    if (next.done) throw new Error('Truncated installed image');
                    pending = next.value; position = 0;
                }
                const count = Math.min(size - written, pending.length - position);
                bytes.set(pending.subarray(position, position + count), written);
                position += count; written += count;
            }
            return bytes;
        }
        try {
            const prefix = await read(4), length = new DataView(prefix.buffer).getUint32(0, true);
            if (length === 0 || length > 16 * 1024 * 1024) throw new Error('Invalid installed image header');
            const header = JSON.parse(new TextDecoder('utf-8', {fatal:true}).decode(await read(length)));
            if (header.version !== 1 || header.game !== expected.game
                || !header.sources || Object.keys(header.sources).length !== Object.keys(expected.sources).length
                || Object.entries(expected.sources).some(([key, value]) => header.sources[key] !== value)
                || !Array.isArray(header.entries) || header.entries.length > 100000)
                throw new Error('Installed image does not match this game/device');
            const paths = new Map();
            let total = 0;
            for (const entry of header.entries) {
                if (typeof entry.path !== 'string' || !entry.path.startsWith('/data/')
                    || entry.path.includes('\\') || entry.path.includes('\0')
                    || entry.path.split('/').slice(1).some(part => !part || part === '.' || part === '..')
                    || paths.has(entry.path) || !['directory','file'].includes(entry.type)
                    || !Number.isSafeInteger(entry.size) || entry.size < 0
                    || (entry.type === 'directory' && entry.size !== 0))
                    throw new Error('Invalid installed image entry');
                paths.set(entry.path, entry.type);
                total += entry.size;
                if (total > 1024 * 1024 * 1024) throw new Error('Installed image too large');
            }
            for (const entry of header.entries) {
                for (let parent = entry.path.slice(0, entry.path.lastIndexOf('/')); parent !== '/data';
                    parent = parent.slice(0, parent.lastIndexOf('/'))) {
                    if (paths.get(parent) !== 'directory') throw new Error('Missing installed image directory');
                }
            }
            filesystem.mkdirTree('/data');
            for (const entry of header.entries) {
                if (entry.type === 'directory') filesystem.mkdirTree(entry.path);
            }
            for (const entry of header.entries) {
                if (entry.type !== 'file') continue;
                const bytes = await read(entry.size);
                const stream = filesystem.open(entry.path, 'w');
                filesystem.close(stream);
                if (bytes.length) filesystem.writeFile(entry.path, bytes);
            }
            if (position !== pending.length || !(await reader.read()).done)
                throw new Error('Trailing installed image data');
            return {files:header.entries.filter(entry => entry.type === 'file').length, bytes:total};
        } finally {
            await reader.cancel();
            reader.releaseLock();
        }
    }
};
