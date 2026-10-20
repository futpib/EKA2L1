import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";
import { buildDir, startServer, compilerPolicyFromEnv } from "./server.ts";
import { fetchCid } from "@futpib/fetch-cid";
import { loadInstalledImage } from './installed-image.ts';
import { loadRomCache } from './rom-cache.ts';
import { refreshAsset } from './asset-cache.ts';

if (!fs.existsSync(path.join(buildDir, "eka2l1.html"))) {
  console.error("build-wasm output not found. Run the WASM build first.");
  process.exit(1);
}

const ROM_CID = "bafybeicj2jkrjfirzdz5jezz6hjbx2ylyv343kaecnhytl3g6yjy3mwmqm";
const RPKG_CID = "bafybeihjy4vjxb5cy7zxca4kedg5ncxf5xrbj5basirfefemqwru73aipu";
type GameAsset = { file: string; cid: string; sha256: string };
const games = JSON.parse(fs.readFileSync(new URL('./games.json', import.meta.url), 'utf8')) as (GameAsset & {
  id: string; title: string; uid: string;
  earlyRecycleFunctions?: number;
  earlyRecycleMax?: number;
  warmupRecycleFunctions?: number;
  warmupRecycleMax?: number;
  preparedRecycleFunctions?: number;
  preparedRecycleEarlyFunctions?: number;
  preparedRecycleEarlyMax?: number;
  preparedRecycleMax?: number;
  trimIdleStartupMs?: number;
  ngage?: GameAsset & { runtime: GameAsset };
})[];
const gameAssets = new Map(games.flatMap(game => [game, ...(game.ngage
  ? [game.ngage.runtime, game.ngage] : [])]).map(asset => [asset.file, asset]));

async function fetchCidToFile(cid: string, label: string, destPath: string, digest: string): Promise<void> {
  const valid = (data: Buffer) => crypto.createHash('sha256').update(data).digest('hex') === digest;
  if (fs.existsSync(destPath)) {
    if (!valid(fs.readFileSync(destPath))) throw new Error(`${label}: cached SHA-256 mismatch at ${destPath}`);
    console.log(`${label}: cached at ${destPath}`);
    return;
  }
  console.log(`Fetching ${label} (${cid})...`);
  const chunks: Uint8Array[] = [];
  for await (const chunk of await fetchCid(cid)) {
    chunks.push(chunk);
  }
  const buf = Buffer.concat(chunks);
  if (!valid(buf)) throw new Error(`${label}: downloaded SHA-256 mismatch`);
  fs.writeFileSync(destPath + '.tmp', buf);
  fs.renameSync(destPath + '.tmp', destPath);
  console.log(`  ${label}: ${(buf.length / 1e6).toFixed(1)} MB -> ${destPath}`);
}

const cacheDir = process.env.EKA2L1_ASSET_DIR || path.join(os.tmpdir(), "eka2l1-serve");
fs.mkdirSync(cacheDir, { recursive: true });

const romPath = path.join(cacheDir, "SYM.ROM");
const rpkgPath = path.join(cacheDir, "SYM.RPKG");

await Promise.all([
  fetchCidToFile(ROM_CID, "ROM", romPath, '89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490'),
  fetchCidToFile(RPKG_CID, "RPKG", rpkgPath, '58964f3d08a542f01118a7dfb78a34d2e029962b8edb9988381a37994c1c1531'),
  ...[...gameAssets.values()].map(asset => fetchCidToFile(asset.cid, asset.file, path.join(cacheDir, asset.file), asset.sha256)),
]);

const port = parseInt(process.argv[2] ?? "8080", 10);
const appName = process.argv[3];
const defaultGame = appName === undefined ? undefined
  : games.find(game => [game.id, game.title, game.uid].some(value => value.toLowerCase() === appName.toLowerCase()))?.id;
if (appName !== undefined && !defaultGame) throw new Error(`Unknown game: ${appName}`);

const preloadFiles: Record<string, string> = {
  "/preload/rom": romPath,
  "/preload/rpkg": rpkgPath,
  // Retain the Snakes endpoint used by existing benchmark clients.
  "/preload/sis": path.join(cacheDir, 'Snakes.sis'),
  ...Object.fromEntries(games.map(game => ['/preload/games/' + game.id, path.join(cacheDir, game.file)])),
};
// Optional prepared files are local deployment artifacts, never game blobs in
// Git. Missing images keep the existing installer path for that game.
const installedImages = new Map();
const romCaches = new Map();
const policy = compilerPolicyFromEnv();
const romCacheDir = process.env.EKA2L1_ROM_CACHE_DIR || path.join(buildDir, 'rom-cache');
if (fs.existsSync(romCacheDir)) {
  const wasm = await refreshAsset(path.join(buildDir, 'eka2l1.wasm'));
  for (const game of games) {
    try {
      const compiled = await loadRomCache(romCacheDir, game.id, wasm.digest,
        '89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490',
        policy.memoryImpl!, policy.unsafeCode!);
      if (!compiled) continue;
      romCaches.set(game.id, compiled.cache);
      preloadFiles[compiled.cache.url] = compiled.file;
      console.log(game.title + ': ROM translation cache (' + compiled.file + ')');
    } catch (error) { console.warn(game.title + ': ignoring ROM translation cache: ' + String(error)); }
  }
}
if (process.env.EKA2L1_PREINSTALLED_DIR) {
  for (const game of games) {
    if (!game.ngage) continue;
    const prepared = await loadInstalledImage(process.env.EKA2L1_PREINSTALLED_DIR,game.id,{
      rom:'89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490',
      rpkg:'58964f3d08a542f01118a7dfb78a34d2e029962b8edb9988381a37994c1c1531',
      game:game.ngage.sha256,runtime:game.ngage.runtime.sha256,
    });
    if (!prepared) continue;
    installedImages.set(game.id,prepared.image);
    preloadFiles[prepared.image.url] = prepared.file;
    console.log(game.title + ': prepared installation (' + prepared.file + ')');
  }
}
for (const game of games) {
  if (!game.ngage) continue;
  preloadFiles['/preload/games/' + game.id + '/ngage'] = path.join(cacheDir, game.ngage.file);
  preloadFiles['/preload/games/' + game.id + '/runtime'] = path.join(cacheDir, game.ngage.runtime.file);
}

const host = process.env.EKA2L1_SERVE_HOST ?? "127.0.0.1";
const certPath = process.env.EKA2L1_TLS_CERT;
const keyPath = process.env.EKA2L1_TLS_KEY;
if (Boolean(certPath) !== Boolean(keyPath)) {
  throw new Error("Set both EKA2L1_TLS_CERT and EKA2L1_TLS_KEY for HTTPS");
}
const tls = certPath && keyPath
  ? { cert: fs.readFileSync(certPath), key: fs.readFileSync(keyPath) }
  : undefined;
const { port: resolvedPort } = await startServer(port, preloadFiles, undefined, {
  host, tls, compilerPolicy: policy, defaultGame,
  games: games.map(({id, title, uid, ngage, earlyRecycleFunctions, earlyRecycleMax, warmupRecycleFunctions, warmupRecycleMax, preparedRecycleFunctions, preparedRecycleEarlyFunctions, preparedRecycleEarlyMax, preparedRecycleMax, trimIdleStartupMs}) => ({id, title, uid,
    ...(earlyRecycleFunctions ? {earlyRecycleFunctions} : {}),
    ...(earlyRecycleMax ? {earlyRecycleMax} : {}),
    ...(warmupRecycleFunctions ? {warmupRecycleFunctions} : {}),
    ...(warmupRecycleMax ? {warmupRecycleMax} : {}),
    ...(preparedRecycleFunctions ? {preparedRecycleFunctions} : {}),
    ...(preparedRecycleEarlyFunctions ? {preparedRecycleEarlyFunctions} : {}),
    ...(preparedRecycleEarlyMax ? {preparedRecycleEarlyMax} : {}),
    ...(preparedRecycleMax ? {preparedRecycleMax} : {}),
    ...(trimIdleStartupMs ? {trimIdleStartupMs} : {}),
    ...(installedImages.has(id) ? {image:installedImages.get(id)} : {}),
    ...(romCaches.has(id) ? {romCache:romCaches.get(id)} : {}),
    sis: '/preload/games/' + id + (ngage ? '/runtime' : ''),
    ...(ngage ? {ngage: '/preload/games/' + id + '/ngage'} : {}),
  })),
});
const displayHost = process.env.EKA2L1_SERVE_NAME ?? host;
const urlHost = displayHost.includes(":") ? `[${displayHost}]` : displayHost;
console.log(`\nServing EKA2L1 WASM at ${tls ? "https" : "http"}://${urlHost}:${resolvedPort}/`);
if (!tls && !["127.0.0.1", "::1", "localhost"].includes(host)) {
  console.warn("LAN browsers require HTTPS with a trusted certificate for WASM threads.");
}
console.log(`Games: ${games.map(game => game.title).join(', ')}`);
console.log("Press Ctrl+C to stop.");
