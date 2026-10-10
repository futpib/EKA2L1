import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";
import { buildDir, startServer, compilerPolicyFromEnv } from "./server.ts";
import { fetchCid } from "@futpib/fetch-cid";
import { ngagePackage } from './ngage.ts';

if (!fs.existsSync(path.join(buildDir, "eka2l1.html"))) {
  console.error("build-wasm output not found. Run the WASM build first.");
  process.exit(1);
}

const ROM_CID = "bafybeicj2jkrjfirzdz5jezz6hjbx2ylyv343kaecnhytl3g6yjy3mwmqm";
const RPKG_CID = "bafybeihjy4vjxb5cy7zxca4kedg5ncxf5xrbj5basirfefemqwru73aipu";
type GameAsset = { file: string; cid: string; sha256: string };
const games = JSON.parse(fs.readFileSync(new URL('./games.json', import.meta.url), 'utf8')) as (GameAsset & {
  id: string; title: string; uid: string;
  ngage?: { runtime: GameAsset; metadata: GameAsset; sha256: string };
})[];
const gameAssets = new Map(games.flatMap(game => [game, ...(game.ngage
  ? [game.ngage.runtime, game.ngage.metadata] : [])]).map(asset => [asset.file, asset]));

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
for (const game of games) {
  if (!game.ngage) continue;
  const bytes = ngagePackage(fs.readFileSync(path.join(cacheDir, game.ngage.metadata.file)),
    fs.readFileSync(path.join(cacheDir, game.file)));
  if (crypto.createHash('sha256').update(bytes).digest('hex') !== game.ngage.sha256)
    throw new Error(`${game.title}: reconstructed N-Gage package SHA-256 mismatch`);
  const packagePath = path.join(cacheDir, game.id + '.n-gage');
  fs.writeFileSync(packagePath + '.tmp', bytes);
  fs.renameSync(packagePath + '.tmp', packagePath);
  preloadFiles['/preload/games/' + game.id + '/ngage'] = packagePath;
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
  host, tls, compilerPolicy: compilerPolicyFromEnv(), defaultGame,
  games: games.map(({id, title, uid, ngage}) => ({id, title, uid,
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
