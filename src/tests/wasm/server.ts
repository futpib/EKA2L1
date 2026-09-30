import http from "node:http";
import crypto from "node:crypto";
import { refreshAsset, notModified, assetCacheScript, type Asset } from "./asset-cache.ts";
import https from "node:https";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export const buildDir = process.env.EKA2L1_WASM_BUILD_DIR
  ? path.resolve(process.env.EKA2L1_WASM_BUILD_DIR)
  : path.resolve(__dirname, "../../../build-wasm/src/emu/wasm");

const MIME_TYPES: Record<string, string> = {
  ".html": "text/html",
  ".js": "application/javascript",
  ".wasm": "application/wasm",
  ".map": "application/json",
  ".ico": "image/x-icon",
};

function getMime(filePath: string): string {
  for (const [ext, mime] of Object.entries(MIME_TYPES)) {
    if (filePath.endsWith(ext)) return mime;
  }
  return "application/octet-stream";
}

function makeAutoStartScript(appName?: string): string {
  if (!appName) return "";
  const encodedName = JSON.stringify(appName).replace(/</g, "\\u003c");
  return `
<script>
async function autoStart() {
  while (typeof Module === 'undefined' || !Module.calledRun)
    await new Promise(resolve => setTimeout(resolve, 100));
  try {
    // Use the same live configuration, graphics handshake and input activation
    // as a manual Start. Keep only preload-file selection in this helper.
    for (const [endpoint, selector, name] of [
      ['/preload/rom', 'rom-file', 'SYM.ROM'],
      ['/preload/rpkg', 'rpkg-file', 'SYM.RPKG'],
      ['/preload/sis', 'sis-file', 'app.sis']]) {
      const response = await window.ekaDownload(endpoint);
      if (response.status === 404) continue;
      if (!response.ok) throw new Error(endpoint + ': HTTP ' + response.status);
      const transfer = new DataTransfer();
      transfer.items.add(new File([await response.blob()], name));
      document.getElementById(selector).files = transfer.files;
    }
    document.getElementById('app-name').value = ${encodedName};
    await startEmulator();
  } catch (error) {
    Module.setStatus('Error: ' + error.message);
    Module.printErr(error.toString());
  }
}
autoStart();
</script>`;
}

export type CompilerPolicy = { irMode?: number; eagerRegions?: number; tlbHash?: number };

export function compilerPolicyFromEnv(): CompilerPolicy | undefined {
  const ir = process.env.EKA2L1_AOT_IR_MODE;
  const eager = process.env.EKA2L1_AOT_EAGER_REGIONS;
  const tlb = process.env.EKA2L1_TLB_HASH;
  if (ir === undefined && eager === undefined && tlb === undefined) return undefined;
  const policy: CompilerPolicy = {};
  if (ir !== undefined) {
    if (!/^(?:-1|[0-9]|10|11|12|13|14|15)$/.test(ir)) throw new Error("Invalid compiler policy");
    policy.irMode = Number(ir);
  }
  if (eager !== undefined) {
    if (!/^[01]$/.test(eager)) throw new Error("Invalid eager-region policy");
    policy.eagerRegions = Number(eager);
  }
  if (tlb !== undefined) {
    if (!/^[01]$/.test(tlb)) throw new Error("Invalid TLB index policy");
    policy.tlbHash = Number(tlb);
  }
  return policy;
}

function makeCompilerPolicyScript(policy?: CompilerPolicy): string {
  if (!policy) return "";
  if ((policy.irMode !== undefined && (!Number.isInteger(policy.irMode) || policy.irMode < -1 || policy.irMode > 15))
      || (policy.eagerRegions !== undefined && ![0,1].includes(policy.eagerRegions))
      || (policy.tlbHash !== undefined && ![0,1].includes(policy.tlbHash)))
    throw new Error("Invalid compiler policy");
  return `<script>
window.ekaCompilerPolicy = {requested:${JSON.stringify(policy)}, applied:false};
{
  const originalStart = startEmulator;
  startEmulator = async function() {
    const state = window.ekaCompilerPolicy;
    if (!state.applied) {
      for (const [key, entry] of [['irMode','eka2l1_ir_configure'], ['eagerRegions','eka2l1_eager_regions_configure'], ['tlbHash','eka2l1_tlb_hash_configure']]) {
        if (state.requested[key] === undefined) continue;
        if (typeof Module['_' + entry] !== 'function'
            || Module.ccall(entry, 'number', ['number'], [state.requested[key]]) !== 0)
          throw new Error('Emulator compiler configuration failed: ' + key);
      }
      state.applied = true;
    }
    return originalStart();
  };
}
</script>`;
}

export async function startServer(
  port = 0,
  preloadFiles: Record<string, string> = {},
  appName?: string,
  options: { host?: string; tls?: https.ServerOptions; compilerPolicy?: CompilerPolicy } = {},
): Promise<{ server: http.Server; port: number }> {
  const assets = new Map<string, Asset>();
  const files: Record<string, string> = { ...preloadFiles };
  for (const name of fs.readdirSync(buildDir)) {
    if (/\.(?:js|wasm|data)$/.test(name)) files['/' + name] = path.join(buildDir, name);
  }
  for (const [url, file] of Object.entries(files)) {
    if (fs.existsSync(file) && fs.statSync(file).isFile()) assets.set(url, await refreshAsset(file));
  }
  const autoStartScript = makeCompilerPolicyScript(options.compilerPolicy) + makeAutoStartScript(appName);
  return new Promise((resolve, reject) => {
    const handler: http.RequestListener = (req, res) => {
      void respond(req, res).catch(error => {
        console.error('Asset response failed:', error);
        if (!res.headersSent) res.writeHead(500, { 'Cache-Control': 'no-store' });
        res.end('Asset response failed');
      });
    };
    async function respond(req: http.IncomingMessage, res: http.ServerResponse) {
      res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
      res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      res.setHeader('Cache-Control', 'no-cache');
      if (req.method !== 'GET' && req.method !== 'HEAD') {
        res.writeHead(405, { Allow: 'GET, HEAD' }).end(); return;
      }
      const requestUrl = new URL(req.url || '/', 'http://localhost');
      let urlPath = requestUrl.pathname;
      if (urlPath === '/') urlPath = '/eka2l1.html';
      if (urlPath === '/favicon.ico') {
        const icon = path.resolve(__dirname, '../../emu/qt/duck_tank.ico');
        if (!fs.existsSync(icon)) { res.writeHead(204).end(); return; }
        await sendFile(icon); return;
      }
      if (preloadFiles[urlPath]) {
        if (!fs.existsSync(preloadFiles[urlPath])) { res.writeHead(404).end('Preload file not found'); return; }
        await sendFile(preloadFiles[urlPath], urlPath); return;
      }
      let file = path.resolve(buildDir, '.' + urlPath);
      if (!file.startsWith(buildDir + path.sep)) { res.writeHead(403).end('Forbidden'); return; }
      if (!fs.existsSync(file)) {
        const alternative = path.join(buildDir, path.basename(urlPath));
        if (!fs.existsSync(alternative)) { res.writeHead(404).end('Not found'); return; }
        file = alternative;
      }
      if (!fs.statSync(file).isFile()) { res.writeHead(404).end('Not found'); return; }
      if (file.endsWith('.html')) {
        const urls: Record<string, string> = {};
        for (const [endpoint, asset] of assets) {
          const current = await refreshAsset(asset.file, asset);
          assets.set(endpoint, current);
          urls[endpoint] = endpoint + '?v=' + current.digest;
        }
        let html = fs.readFileSync(file, 'utf8');
        // Install locateFile before either package loading or the async loader.
        html = html.replace(/<head\b[^>]*>/i, tag => tag + assetCacheScript(urls));
        html = html.replace(/\bsrc=(['"]?)([^'"\s>]+)\1/g, (tag, quote, value) => {
          const endpoint = '/' + value.replace(/^\//, '');
          return urls[endpoint] ? 'src="' + urls[endpoint] + '"' : tag;
        });
        html = html.replace('</body>', autoStartScript + '\n</body>');
        const etag = '"' + crypto.createHash('sha256').update(html).digest('hex') + '"';
        res.setHeader('ETag', etag);
        res.setHeader('Content-Type', 'text/html');
        if (notModified(req, etag)) { res.writeHead(304).end(); return; }
        res.setHeader('Content-Length', Buffer.byteLength(html));
        res.writeHead(200).end(req.method === 'HEAD' ? undefined : html); return;
      }
      await sendFile(file, '/' + path.basename(file));

      async function sendFile(filePath: string, endpoint?: string) {
        const previous = endpoint ? assets.get(endpoint) : undefined;
        const asset = await refreshAsset(filePath, previous);
        if (endpoint) assets.set(endpoint, asset);
        const version = requestUrl.searchParams.get('v');
        if (version !== null && version !== asset.digest) {
          res.writeHead(409, { 'Cache-Control': 'no-store' }).end('Asset version changed; reload the launcher'); return;
        }
        const etag = '"' + asset.digest + '"';
        res.setHeader('ETag', etag);
        res.setHeader('Content-Type', getMime(filePath));
        res.setHeader('Cache-Control', version ? 'public, max-age=31536000, immutable' : 'no-cache');
        if (notModified(req, etag)) { res.writeHead(304).end(); return; }
        res.setHeader('Content-Length', asset.size);
        res.writeHead(200);
        if (req.method === 'HEAD') { res.end(); return; }
        const stream = fs.createReadStream(filePath);
        stream.on('error', error => res.destroy(error));
        res.on('close', () => stream.destroy());
        stream.pipe(res);
      }
    }
    const server = options.tls
      ? https.createServer(options.tls, handler)
      : http.createServer(handler);
    server.once("error", reject);

    server.listen(port, options.host ?? "127.0.0.1", () => {
      const addr = server.address();
      const resolvedPort = typeof addr === "object" && addr ? addr.port : 0;
      resolve({ server, port: resolvedPort });
    });
  });
}
