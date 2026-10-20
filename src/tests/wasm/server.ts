import {configureWatchdog, watchdogInterval} from './watchdog.ts';
import http from "node:http";
import crypto from "node:crypto";
import { refreshAsset, notModified, assetCacheScript, type Asset } from "./asset-cache.ts";
import https from "node:https";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import type {RomCache} from './rom-cache.ts';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export const buildDir = process.env.EKA2L1_WASM_BUILD_DIR
  ? path.resolve(process.env.EKA2L1_WASM_BUILD_DIR)
  : path.resolve(__dirname, "../../../build-wasm/src/emu/wasm");

const MIME_TYPES: Record<string, string> = {
  ".html": "text/html",
  ".js": "application/javascript",
  ".css": "text/css",
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

export type CompilerPolicy = { watchdogUs?: number; unsafeCode?: number; memoryImpl?: number };

export type LauncherGame = {
  id: string; title: string; uid: string; sis: string; ngage?: string;
  trimIdleStartupMs?: number;
  image?: { url: string; game: string; sources: Record<string, string> };
  romCache?: RomCache;
};

function makeGameLauncherScript(games: LauncherGame[], defaultGame?: string): string {
  const encode = (value: unknown) => JSON.stringify(value).replace(/</g, '\\u003c');
  return `<script>configureGameLauncher(${encode(games)}, ${encode(defaultGame ?? null)});</script>`;
}

export function rejectRetiredCompilerOptions(): void {
  for (const name of ['EKA2L1_ARENA_GROWTH', 'EKA2L1_AOT_VERIFY', 'EKA2L1_AOT_DIAGNOSTICS', 'EKA2L1_PROFILE_DETAIL', 'EKA2L1_GUEST_PROFILE', 'EKA2L1_EXIT_CENSUS', 'EKA2L1_AOT_IR_MODE', 'EKA2L1_PREDICATED_LEAVES', 'EKA2L1_LEAF_FEATURES', 'EKA2L1_CODE_COMPARE', 'EKA2L1_ARM_EXCLUSIVE', 'EKA2L1_SPARSE_ROM_LOOKUP', 'EKA2L1_COMPILED_SVC', 'EKA2L1_HOTPATH', 'EKA2L1_THUMB_MEMORY', 'EKA2L1_AOT_HOT', 'EKA2L1_AOT_REGION', 'EKA2L1_AOT_CHAIN', 'EKA2L1_AOT_RAM', 'EKA2L1_ENTRY_BUDGET', 'EKA2L1_DIVISION_DIGITS', 'EKA2L1_ENTRY_ONLY_PRUNING', 'EKA2L1_EXECUTION_LIMITS', 'EKA2L1_TLB_HASH', 'EKA2L1_MEMORY_CACHE', 'EKA2L1_SYNCHRONOUS_COMPILATION', 'EKA2L1_ROM_DISPATCH', 'EKA2L1_CODE_WRITE_PROTECT', 'EKA2L1_CODE_LOOKUP', 'EKA2L1_OMIT_GUARD_PUBLICATION', 'EKA2L1_COMPILED_MEMORY_MISSES', 'EKA2L1_ROM_CALLS', 'EKA2L1_ROM_LEAVES', 'EKA2L1_AOT_EAGER_REGIONS', 'EKA2L1_SNAKES_N80_NATIVE_RESOLUTION', 'EKA2L1_ARM_MEMORY']) {
    if (process.env[name] !== undefined) throw Error('Retired compiler option: ' + name);
  }
}

// Runtime compatibility choices are frozen before initialization.
export function compilerPolicyFromEnv(): CompilerPolicy {
  rejectRetiredCompilerOptions();
  const unsafe = process.env.EKA2L1_UNSAFE_CODE ?? '3';
  const memory = process.env.EKA2L1_MEMORY_IMPL ?? (unsafe === '0' ? '0' : '2');
  if (!/^[03]$/.test(unsafe)) throw Error('Invalid executable-byte policy');
  if (!/^[02]$/.test(memory)) throw Error('Invalid memory implementation policy');
  return {watchdogUs: watchdogInterval(), unsafeCode: Number(unsafe), memoryImpl: Number(memory)};
}

function makeCompilerPolicyScript(policy?: CompilerPolicy): string {
  policy ??= {};
  const allowed = ['watchdogUs', 'unsafeCode', 'memoryImpl'];
  if (Object.keys(policy).some(key => !allowed.includes(key))) throw Error('Invalid compiler policy');
  if ((policy.watchdogUs !== undefined && (!Number.isSafeInteger(policy.watchdogUs) || policy.watchdogUs < 1 || policy.watchdogUs > 1000000))
      || (policy.memoryImpl !== undefined && ![0,2].includes(policy.memoryImpl))
      || (policy.unsafeCode !== undefined && ![0,3].includes(policy.unsafeCode)))
    throw Error('Invalid compiler policy');
  return `<script>
window.ekaCompilerPolicy = {requested:${JSON.stringify(policy)}, applied:false};
{
  const originalStart = startEmulator;
  startEmulator = async function(...args) {
    const state = window.ekaCompilerPolicy;
    if (!state.applied) {
      await (${configureWatchdog.toString()})(state.requested.watchdogUs ?? 2000);
      for (const [key, entry] of [['unsafeCode','eka2l1_unsafe_code_configure'], ['memoryImpl','eka2l1_memory_impl_configure']]) {
        if (state.requested[key] === undefined) continue;
        if (typeof Module['_' + entry] !== 'function'
            || Module.ccall(entry, 'number', ['number'], [state.requested[key]]) !== 0)
          throw new Error('Emulator compiler configuration failed: ' + key);
      }
      state.observed = {};
      for (const [key, entry] of [['unsafeCode','eka2l1_unsafe_code_report'], ['memoryImpl','eka2l1_memory_impl_report']]) {
        if (state.requested[key] === undefined) continue;
        if (typeof Module['_' + entry] !== 'function') throw new Error('Emulator compiler readback unavailable: ' + key);
        state.observed[key] = Module.ccall(entry, 'number', [], []);
        if (state.observed[key] !== state.requested[key]) throw new Error('Emulator compiler readback mismatch: ' + key);
      }
      state.applied = true;
    }
    return originalStart(...args);
  };
}
</script>`;
}

export async function startServer(
  port = 0,
  preloadFiles: Record<string, string> = {},
  appName?: string,
  options: { host?: string; tls?: https.ServerOptions; compilerPolicy?: CompilerPolicy; games?: LauncherGame[]; defaultGame?: string } = {},
): Promise<{ server: http.Server; port: number }> {
  const assets = new Map<string, Asset>();
  const files: Record<string, string> = { ...preloadFiles };
  for (const name of fs.readdirSync(buildDir)) {
    if (/\.(?:js|css|wasm|data)$/.test(name)) files['/' + name] = path.join(buildDir, name);
  }
  for (const [url, file] of Object.entries(files)) {
    if (fs.existsSync(file) && fs.statSync(file).isFile()) assets.set(url, await refreshAsset(file));
  }
  const defaultPolicy = options.compilerPolicy ?? compilerPolicyFromEnv();
  const defaultPolicyScript = makeCompilerPolicyScript(defaultPolicy);
  const autoStartScript = options.games?.length
    ? makeGameLauncherScript(options.games, options.defaultGame) : makeAutoStartScript(appName);
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
      if (urlPath === '/install-handoff') {
        res.setHeader('Content-Type', 'text/html; charset=utf-8');
        res.setHeader('Cache-Control', 'no-store');
        res.end('<!doctype html><title>Finishing installation</title><p>Finishing installation...</p><script>setTimeout(() => location.replace("/" + location.search), 3000)</script>');
        return;
      }
      if (urlPath === '/') urlPath = '/eka2l1.html';
      if (urlPath === '/watchdog.js') {
        await sendFile(path.join(__dirname, 'watchdog.js')); return;
      }
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
        let policyScript = defaultPolicyScript;
        const urls: Record<string, string> = {};
        for (const [endpoint, asset] of assets) {
          const current = await refreshAsset(asset.file, asset);
          assets.set(endpoint, current);
          urls[endpoint] = endpoint + '?v=' + current.digest;
        }
        let html = fs.readFileSync(file, 'utf8');
        // Install locateFile before either package loading or the async loader.
        html = html.replace(/<head\b[^>]*>/i, tag => tag + assetCacheScript(urls));
        html = html.replace(/\b(src|href)=(['"]?)([^'"\s>]+)\2/g, (tag, attribute, quote, value) => {
          const endpoint = '/' + value.replace(/^\//, '');
          return urls[endpoint] ? attribute + '="' + urls[endpoint] + '"' : tag;
        });
        html = html.replace('</body>', policyScript + autoStartScript + '\n</body>');
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
