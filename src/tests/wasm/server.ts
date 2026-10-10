import {configureWatchdog, watchdogInterval} from './watchdog.ts';
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

export const compilerDefaults = {sparseRom: 1, compiledSvc: 1, thumbMemory: 1, irMode: 7, hotpath: 2, predicatedLeaves: 1, leafFeatures: 224, executionLimits: '512,32,8,0'} as const;

export type CompilerPolicy = { watchdogUs?: number; sparseRom?: number; compiledSvc?: number; hotpath?: number; thumbMemory?: number; irMode?: number; codeCompare?: number; predicatedLeaves?: number; leafFeatures?: number; unsafeCode?: number; memoryImpl?: number; executionLimits?: [number,number,number,number] };

export type LauncherGame = { id: string; title: string; uid: string; sis: string };

function makeGameLauncherScript(games: LauncherGame[], defaultGame?: string): string {
  const encode = (value: unknown) => JSON.stringify(value).replace(/</g, '\\u003c');
  return `<script>configureGameLauncher(${encode(games)}, ${encode(defaultGame ?? null)});</script>`;
}

function validExecutionLimits(limits: unknown): limits is [number,number,number,number] {
  return Array.isArray(limits) && limits.length === 4
    && limits.every((value, index) => value === [512,32,8,0][index]);
}

export function rejectRetiredCompilerOptions(): void {
  for (const name of ['EKA2L1_ENTRY_BUDGET', 'EKA2L1_DIVISION_DIGITS', 'EKA2L1_ENTRY_ONLY_PRUNING', 'EKA2L1_EXECUTION_LIMITS', 'EKA2L1_TLB_HASH', 'EKA2L1_MEMORY_CACHE', 'EKA2L1_SYNCHRONOUS_COMPILATION', 'EKA2L1_ROM_DISPATCH', 'EKA2L1_CODE_WRITE_PROTECT', 'EKA2L1_CODE_LOOKUP', 'EKA2L1_OMIT_GUARD_PUBLICATION', 'EKA2L1_COMPILED_MEMORY_MISSES', 'EKA2L1_ROM_CALLS', 'EKA2L1_ROM_LEAVES', 'EKA2L1_AOT_EAGER_REGIONS', 'EKA2L1_SNAKES_N80_NATIVE_RESOLUTION', 'EKA2L1_ARM_MEMORY']) {
    if (process.env[name] !== undefined) throw Error('Retired compiler option: ' + name);
  }
}

// The four graduated optimizations have fixed normal-launch defaults. Explicit
// compilerPolicy overrides and the raw configure APIs remain available to tests.
export function compilerPolicyFromEnv(): CompilerPolicy {
  rejectRetiredCompilerOptions();
  for (const name of ['EKA2L1_SPARSE_ROM_LOOKUP', 'EKA2L1_COMPILED_SVC', 'EKA2L1_HOTPATH', 'EKA2L1_THUMB_MEMORY']) {
    if (process.env[name] !== undefined)
      throw Error('Retired normal-launch option: ' + name + '; use benchmark.ts or profile.ts for targeted controls');
  }
  const ir = process.env.EKA2L1_AOT_IR_MODE ?? String(compilerDefaults.irMode);
  const compare = process.env.EKA2L1_CODE_COMPARE;
  const predicates = process.env.EKA2L1_PREDICATED_LEAVES ?? String(compilerDefaults.predicatedLeaves);
  const features = process.env.EKA2L1_LEAF_FEATURES ?? String(compilerDefaults.leafFeatures);
  const limits = compilerDefaults.executionLimits;
  const unsafe = process.env.EKA2L1_UNSAFE_CODE ?? '3';
  const memory = process.env.EKA2L1_MEMORY_IMPL ?? (unsafe === '0' ? '0' : undefined);
  if (!/^[03]$/.test(unsafe)) throw new Error("Invalid executable-byte policy");
  const policy: CompilerPolicy = {
    compiledSvc: compilerDefaults.compiledSvc, sparseRom: compilerDefaults.sparseRom,
    hotpath: compilerDefaults.hotpath, thumbMemory: compilerDefaults.thumbMemory,
    watchdogUs: watchdogInterval(),
  };
  if (ir !== undefined) {
    if (!/^(?:-1|0|[4-7])$/.test(ir)) throw new Error("Invalid compiler policy");
    policy.irMode = Number(ir);
  }

  if (compare !== undefined) {
    if (!/^[02]$/.test(compare)) throw new Error("Invalid exact comparison policy");
    policy.codeCompare = Number(compare);
  }

  if (predicates !== undefined) {
    if (!/^[01]$/.test(predicates)) throw new Error("Invalid leaf predication policy");
    policy.predicatedLeaves = Number(predicates);
  }
  if (features !== undefined) {
    if (!/^(?:0|32|64|96|128|160|192|224)$/.test(features)) throw new Error("Invalid leaf features policy");
    policy.leafFeatures = Number(features);
  }
  if (limits !== undefined) {
    const parsed = limits.split(',').map(Number);
    if (!validExecutionLimits(parsed) || parsed.join(',') !== limits) throw new Error("Invalid execution limits policy");
    policy.executionLimits = parsed;
  }
  if (memory !== undefined) {
    if (!/^[02]$/.test(memory)) throw Error('Invalid memory implementation policy');
    policy.memoryImpl = Number(memory);
  }
  policy.unsafeCode = Number(unsafe);
  return policy;
}

function makeCompilerPolicyScript(policy?: CompilerPolicy): string {
  policy ??= {};
  const allowed = ['watchdogUs','sparseRom','compiledSvc','hotpath','thumbMemory','irMode','codeCompare','predicatedLeaves','leafFeatures','unsafeCode','memoryImpl','executionLimits'];
  if (Object.keys(policy).some(key => !allowed.includes(key))) throw Error('Invalid compiler policy');
  if ((policy.watchdogUs !== undefined && (!Number.isSafeInteger(policy.watchdogUs) || policy.watchdogUs < 1 || policy.watchdogUs > 1000000))
      || (policy.sparseRom !== undefined && ![0,1].includes(policy.sparseRom))
      || (policy.compiledSvc !== undefined && ![0,1].includes(policy.compiledSvc))
      || (policy.hotpath !== undefined && ![0,2].includes(policy.hotpath))
      || (policy.thumbMemory !== undefined && ![0,1].includes(policy.thumbMemory))
      || (policy.irMode !== undefined && ![-1,0,4,5,6,7].includes(policy.irMode))
      || (policy.codeCompare !== undefined && ![0,2].includes(policy.codeCompare))
      || (policy.memoryImpl !== undefined && ![0,2].includes(policy.memoryImpl))
      || (policy.unsafeCode !== undefined && ![0,3].includes(policy.unsafeCode))
      || (policy.predicatedLeaves !== undefined && ![0,1].includes(policy.predicatedLeaves))
      || (policy.leafFeatures !== undefined && ![0,32,64,96,128,160,192,224].includes(policy.leafFeatures))
      || (policy.executionLimits !== undefined && !validExecutionLimits(policy.executionLimits)))
    throw new Error("Invalid compiler policy");
  return `<script>
window.ekaCompilerPolicy = {requested:${JSON.stringify(policy)}, applied:false};
{
  const originalStart = startEmulator;
  startEmulator = async function() {
    const state = window.ekaCompilerPolicy;
    if (!state.applied) {
      await (${configureWatchdog.toString()})(state.requested.watchdogUs ?? 2000);
      for (const [key, entry] of [['sparseRom','eka2l1_sparse_rom_lookup_configure'], ['compiledSvc','eka2l1_compiled_svc_configure'], ['hotpath','eka2l1_hotpath_configure'], ['thumbMemory','eka2l1_thumb_memory_configure'], ['irMode','eka2l1_ir_configure'], ['codeCompare','eka2l1_code_compare_configure'], ['predicatedLeaves','eka2l1_leaf_predication_configure'], ['leafFeatures','eka2l1_leaf_features_configure'], ['unsafeCode','eka2l1_unsafe_code_configure'], ['memoryImpl','eka2l1_memory_impl_configure']]) {
        if (state.requested[key] === undefined) continue;
        if (typeof Module['_' + entry] !== 'function'
            || Module.ccall(entry, 'number', ['number'], [state.requested[key]]) !== 0)
          throw new Error('Emulator compiler configuration failed: ' + key);
      }
      state.observed = {};
      for (const [key, entry] of [['sparseRom','eka2l1_sparse_rom_lookup_report'], ['compiledSvc','eka2l1_compiled_svc_report'], ['hotpath','eka2l1_hotpath_report'], ['thumbMemory','eka2l1_thumb_memory_report'], ['predicatedLeaves','eka2l1_leaf_predication_report'], ['leafFeatures','eka2l1_leaf_features_report'], ['unsafeCode','eka2l1_unsafe_code_report'], ['memoryImpl','eka2l1_memory_impl_report']]) {
        if (state.requested[key] === undefined) continue;
        if (typeof Module['_' + entry] !== 'function') throw new Error('Emulator compiler readback unavailable: ' + key);
        state.observed[key] = Module.ccall(entry, 'number', [], []);
        if (state.observed[key] !== state.requested[key]) throw new Error('Emulator compiler readback mismatch: ' + key);
      }
      if (state.requested.executionLimits !== undefined) {
        const limits = state.requested.executionLimits;
        if (typeof Module._eka2l1_execution_limits_report !== 'function') throw new Error('Emulator compiler readback unavailable: executionLimits');
        const observed = Module.ccall('eka2l1_execution_limits_report', 'string', [], []);
        if (observed !== limits.join(',')) throw new Error('Emulator compiler readback mismatch: executionLimits');
        state.observed.executionLimits = observed.split(',').map(Number);
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
