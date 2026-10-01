import crypto from 'node:crypto';
import fs from 'node:fs';
import type http from 'node:http';

export type Asset = { file: string; fingerprint: string; digest: string; size: number };

export async function refreshAsset(file: string, previous?: Asset): Promise<Asset> {
  const stat = fs.statSync(file);
  const fingerprint = [stat.dev, stat.ino, stat.size, stat.mtimeMs, stat.ctimeMs].join(':');
  if (previous?.fingerprint === fingerprint) return previous;
  const hash = crypto.createHash('sha256');
  for await (const bytes of fs.createReadStream(file)) hash.update(bytes);
  return { file, fingerprint, digest: hash.digest('hex'), size: stat.size };
}

export function notModified(req: http.IncomingMessage, etag: string): boolean {
  return String(req.headers['if-none-match'] || '').split(',')
    .some(value => value.trim() === '*' || value.trim().replace(/^W\//, '') === etag);
}

// Cache Storage is explicit because large ROM packages can exceed the browser's
// per-entry HTTP cache limits. Storage denial/quota errors must not block play.
export function assetCacheScript(urls: Record<string, string>): string {
  const encoded = JSON.stringify(urls).replace(/</g, '\\u003c');
  return `<script>
window.ekaAssetUrls = ${encoded};
window.Module = window.Module || {async: true};
if (!Module.locateFile) Module.locateFile = function(name, prefix) {
  const url = new URL(name, prefix || location.href);
  return window.ekaAssetUrls[url.pathname] || url.href;
};
window.ekaDownload = async function(endpoint) {
  const url = new URL(window.ekaAssetUrls[endpoint] || endpoint, location.href).href;
  let cache;
  try {
    cache = await caches.open('eka2l1-downloads-v1');
    const saved = await cache.match(url);
    if (saved) return saved;
  } catch (error) { console.info('Download cache unavailable; using network', String(error)); }
  const response = await fetch(url);
  if (response.ok && cache && window.ekaAssetUrls[endpoint]) {
    try {
      await cache.put(url, response.clone());
      // Retain the old copy until the replacement has been stored successfully.
      for (const key of await cache.keys()) {
        if (key.url !== url && new URL(key.url).pathname === new URL(url).pathname)
          await cache.delete(key);
      }
    } catch (error) { console.info('Download could not be cached', String(error)); }
  }
  return response;
};
</script>`;
}
