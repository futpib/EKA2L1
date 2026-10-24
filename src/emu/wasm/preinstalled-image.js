// Installed files, not a running-emulator checkpoint. Each launch restores a
// new device's writable filesystem before C++ creates the device/package managers.
globalThis.EkaInstalledImage = {
    chunkBytes: 256 * 1024,
    async restore(response, expected, filesystem, storage = {}) {
        if (!response.ok) throw new Error('Installed image download failed: HTTP ' + response.status);
        const reader = response.body.pipeThrough(new DecompressionStream('gzip')).getReader();
        let pending = new Uint8Array(), position = 0;
        async function read(size, bytes = new Uint8Array(size)) {
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
        const written = [], links = [];
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
                    || (entry.type === 'directory' && entry.size !== 0)
                    || (entry.mode !== undefined && (!Number.isInteger(entry.mode) || entry.mode < 0 || entry.mode > 0o177777))
                    || (entry.modified !== undefined && (!Number.isSafeInteger(entry.modified) || entry.modified < 0)))
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
            const chunk = new Uint8Array(EkaInstalledImage.chunkBytes);
            const write = storage.write || ((stream, bytes, count, offset) => filesystem.write(stream, bytes, 0, count, offset));
            let offloadedBytes = 0;
            for (const entry of header.entries) {
                if (entry.type !== 'file') continue;
                const target = storage.destination?.(entry) || entry.path;
                if (target !== entry.path) filesystem.mkdirTree(target.slice(0, target.lastIndexOf('/')));
                const stream = filesystem.open(target, 'w');
                written.push(target);
                try {
                    for (let offset = 0; offset < entry.size;) {
                        const count = Math.min(chunk.length, entry.size - offset);
                        await read(count, chunk);
                        if (write(stream, chunk, count, offset) !== count)
                            throw new Error('Short installed image write');
                        offset += count;
                    }
                } finally { filesystem.close(stream); }
                if (target !== entry.path) {
                    links.push({path:entry.path,target});
                    offloadedBytes += entry.size;
                }
            }
            if (position !== pending.length || !(await reader.read()).done)
                throw new Error('Trailing installed image data');
            // Publish disk-backed files only after the complete stream, including
            // gzip's checksum, has been validated. Never save a partial image.
            for (const {path, target} of links) {
                filesystem.symlink(target, path);
                written.push(path);
            }
            // Apply directory timestamps last, after creating all their children.
            for (const entry of [...header.entries].reverse()) storage.metadata?.(entry);
            return {files:header.entries.filter(entry => entry.type === 'file').length, bytes:total, offloadedBytes};
        } catch (error) {
            for (const path of written.reverse()) {
                try { filesystem.unlink(path); } catch {}
            }
            throw error;
        } finally {
            await reader.cancel().catch(() => {});
            reader.releaseLock();
        }
    }
};
