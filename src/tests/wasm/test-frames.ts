import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import puppeteer, { type Page } from "puppeteer";
import { fetchCid } from "@futpib/fetch-cid";
import { buildDir, startServer } from "./server.ts";

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

const outDir = path.resolve(buildDir, "../../../build-wasm/frames-wasm");
const PID_FILE = path.join(outDir, "wasm-frames.pid");

function acquirePidLock(): void {
  if (fs.existsSync(PID_FILE)) {
    const oldPid = parseInt(fs.readFileSync(PID_FILE, "utf-8").trim(), 10);
    try {
      process.kill(oldPid, 0);
      console.error(`FAIL: Another wasm-frames test is already running (pid ${oldPid}). Remove ${PID_FILE} if stale.`);
      process.exit(1);
    } catch (err: unknown) {
      if ((err as NodeJS.ErrnoException).code !== "ESRCH") throw err;
      console.log(`Removing stale pid file (pid ${oldPid})`);
    }
  }
  fs.writeFileSync(PID_FILE, String(process.pid));
}

function releasePidLock(): void {
  if (fs.existsSync(PID_FILE) && fs.readFileSync(PID_FILE, "utf-8").trim() === String(process.pid)) {
    fs.unlinkSync(PID_FILE);
  }
}

async function run(): Promise<void> {
  if (!fs.existsSync(path.join(buildDir, "eka2l1.html"))) {
    console.error("build-wasm output not found. Run the WASM build first.");
    process.exit(1);
  }

  fs.mkdirSync(outDir, { recursive: true });
  acquirePidLock();

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

  const preloadFiles: Record<string, string> = {
    "/preload/rom": romPath,
    "/preload/rpkg": rpkgPath,
    "/preload/sis": sisPath,
  };

  const { server, port } = await startServer(0, preloadFiles, "Snakes");
  const url = `http://127.0.0.1:${port}/`;
  console.log(`\nServing at ${url}\n`);

  const browser = await puppeteer.launch({
    headless: true,
    protocolTimeout: 1200_000,
    args: [
      "--no-sandbox",
      "--disable-setuid-sandbox",
      "--use-gl=angle",
      "--use-angle=swiftshader",
      "--enable-features=SharedArrayBuffer",
      "--enable-unsafe-swiftshader",
    ],
  });

  const page: Page = await browser.newPage();
  page.setDefaultTimeout(300_000);

  page.on("console", (msg) => {
    const text = msg.text();
    if (text.includes("Frame dumper:")) {
      console.log(`  [frame] ${text}`);
    }
  });

  try {
    console.log("Loading page (auto-start mode)...");
    await page.goto(url, { waitUntil: "networkidle0", timeout: 60_000 });

    // Wait for Module to be ready
    await page.waitForFunction(
      "typeof Module !== 'undefined' && Module.calledRun === true",
      { timeout: 120_000 },
    );
    console.log("WASM module loaded");

    // Wait for emulator to be running (auto-start handles init/install/run)
    console.log("Waiting for emulator to start...");
    await page.waitForFunction(
      () => {
        const status = document.getElementById("status")?.textContent ?? "";
        return status.includes("Running") || status.includes("Error");
      },
      { timeout: 300_000, polling: 1000 },
    );

    const status = await page.$eval("#status", (el) => el.textContent ?? "");
    console.log(`Status: "${status}"`);
    if (status.includes("Error")) {
      throw new Error(`Emulator error: ${status}`);
    }

    // Start frame dump in emscripten FS
    const emFsDir = "/tmp/frames";
    await page.evaluate((dir: string) => {
      try { FS.mkdir(dir); } catch(e) {}
      // @ts-expect-error Module is a global from Emscripten
      Module.ccall('eka2l1_start_frame_dump', null, ['string', 'number'], [dir, 16]);
    }, emFsDir);
    console.log("Frame dump started");

    // Poll until done
    const timeout = 600_000;
    const start = performance.now();
    while (performance.now() - start < timeout) {
      const done = await page.evaluate(() => {
        // @ts-expect-error Module is a global from Emscripten
        return Module._eka2l1_frame_dump_done ? Module._eka2l1_frame_dump_done() : 0;
      });
      const captured = await page.evaluate(() => {
        // @ts-expect-error Module is a global from Emscripten
        return Module._eka2l1_frame_dump_captured ? Module._eka2l1_frame_dump_captured() : 0;
      });

      const elapsed = ((performance.now() - start) / 1000).toFixed(1);
      console.log(`[${elapsed}s] captured: ${captured}/16, done: ${done}`);

      if (done) break;
      await new Promise((r) => setTimeout(r, 2000));
    }

    // Read PNG files from emscripten FS and save locally
    const files = await page.evaluate((dir: string) => {
      const result: { name: string; data: number[] }[] = [];
      try {
        const entries = FS.readdir(dir);
        for (const entry of entries) {
          if (entry === "." || entry === "..") continue;
          if (entry.endsWith(".png")) {
            const data = FS.readFile(dir + "/" + entry);
            result.push({ name: entry, data: Array.from(data) });
          }
        }
      } catch (e) {}
      return result;
    }, emFsDir);

    for (const file of files) {
      const outPath = path.join(outDir, file.name);
      fs.writeFileSync(outPath, Buffer.from(file.data));
      console.log(`  Saved: ${outPath} (${file.data.length} bytes)`);
    }

    console.log(`\nCaptured ${files.length} frames to ${outDir}`);
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    console.error(`\nFAIL: ${msg}`);
    process.exit(1);
  } finally {
    await browser.close();
    server.close();
    releasePidLock();
  }
}

run();
