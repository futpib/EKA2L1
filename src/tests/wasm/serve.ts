import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { buildDir, startServer } from "./server.ts";
import { fetchCid } from "@futpib/fetch-cid";

if (!fs.existsSync(path.join(buildDir, "eka2l1.html"))) {
  console.error("build-wasm output not found. Run the WASM build first.");
  process.exit(1);
}

const ROM_CID = "bafybeicj2jkrjfirzdz5jezz6hjbx2ylyv343kaecnhytl3g6yjy3mwmqm";
const RPKG_CID = "bafybeihjy4vjxb5cy7zxca4kedg5ncxf5xrbj5basirfefemqwru73aipu";
const SIS_CID = "bafybeicuomcc2zhzi3vwfb5xihnlkikz3biaa43g4d22z2wptcmhvmp3di";

async function fetchCidToFile(cid: string, label: string, destPath: string): Promise<void> {
  if (fs.existsSync(destPath)) {
    console.log(`${label}: cached at ${destPath}`);
    return;
  }
  console.log(`Fetching ${label} (${cid})...`);
  const chunks: Uint8Array[] = [];
  for await (const chunk of await fetchCid(cid)) {
    chunks.push(chunk);
  }
  const buf = Buffer.concat(chunks);
  fs.writeFileSync(destPath, buf);
  console.log(`  ${label}: ${(buf.length / 1e6).toFixed(1)} MB -> ${destPath}`);
}

const cacheDir = path.join(os.tmpdir(), "eka2l1-serve");
fs.mkdirSync(cacheDir, { recursive: true });

const romPath = path.join(cacheDir, "SYM.ROM");
const rpkgPath = path.join(cacheDir, "SYM.RPKG");
const sisPath = path.join(cacheDir, "Snakes.sis");

await Promise.all([
  fetchCidToFile(ROM_CID, "ROM", romPath),
  fetchCidToFile(RPKG_CID, "RPKG", rpkgPath),
  fetchCidToFile(SIS_CID, "SIS", sisPath),
]);

const port = parseInt(process.argv[2] ?? "8080", 10);
const appName = process.argv[3] ?? "Snakes";

const preloadFiles: Record<string, string> = {
  "/preload/rom": romPath,
  "/preload/rpkg": rpkgPath,
  "/preload/sis": sisPath,
};

const { port: resolvedPort } = await startServer(port, preloadFiles, appName);
console.log(`\nServing EKA2L1 WASM at http://127.0.0.1:${resolvedPort}/`);
console.log(`App: ${appName}`);
console.log("Press Ctrl+C to stop.");
