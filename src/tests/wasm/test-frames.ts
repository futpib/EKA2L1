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

const outDir = path.resolve(buildDir, "../../../frames-wasm");
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

  // Clean old frames and diffs
  for (const f of fs.readdirSync(outDir)) {
    if (f.match(/^frame-.*\.png$/)) {
      fs.unlinkSync(path.join(outDir, f));
    }
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

  const logFile = path.join(outDir, "wasm-frames.log");
  const logStream = fs.createWriteStream(logFile);
  const t0 = performance.now();
  function log(msg: string): void {
    const sec = ((performance.now() - t0) / 1000).toFixed(1);
    const line = `[${sec}s] ${msg}`;
    console.log(line);
    logStream.write(line + "\n");
  }

  page.on("console", (msg) => {
    log(`  [${msg.type()}] ${msg.text()}`);
  });

  page.on("pageerror", (err) => {
    log(`  [pageerror] ${err.message}`);
  });

  try {
    // Retry page load if WASM workers fail to initialize
    const maxRetries = 3;
    for (let attempt = 1; attempt <= maxRetries; attempt++) {
      log(`Loading page (attempt ${attempt}/${maxRetries})...`);
      await page.goto(url, { waitUntil: "domcontentloaded", timeout: 60_000 });
      log("Page DOM loaded");

      // Wait for calledRun with a short timeout
      const bootDeadline = performance.now() + 60_000;
      let booted = false;
      while (performance.now() < bootDeadline) {
        const calledRun = await page.evaluate(() => {
          return typeof Module !== "undefined" && (Module as any).calledRun === true;
        });
        if (calledRun) { booted = true; break; }
        const status = await page.evaluate(() => document.getElementById("status")?.textContent ?? "");
        log(`  waiting for boot: "${status}"`);
        await new Promise((r) => setTimeout(r, 2000));
      }
      if (booted) {
        log("WASM runtime booted");
        break;
      }
      if (attempt < maxRetries) {
        log(`Boot timed out, retrying...`);
        continue;
      }
      throw new Error(`WASM runtime failed to boot after ${maxRetries} attempts (workers may have failed to load)`);
    }

    // Poll for readiness with continuous status reporting
    const emFsDir = "/tmp/frames";
    let dumpStarted = false;
    let dumpDone = false;
    const totalTimeout = 300_000;
    const runTimeout = 120_000;   // "Running" status must appear within 120s
    let runWaitStart = performance.now();

    while (performance.now() - t0 < totalTimeout) {
      const state = await page.evaluate(() => {
        const status = document.getElementById("status")?.textContent ?? "";
        const moduleExists = typeof Module !== "undefined";
        // @ts-expect-error Module is a global from Emscripten
        const calledRun = moduleExists && Module.calledRun === true;
        // @ts-expect-error Module is a global from Emscripten
        const hasDumpFn = calledRun && typeof Module._eka2l1_start_frame_dump === "function";
        // Only call native functions after runtime is initialized
        let dumpDone = 0, dumpCaptured = 0;
        if (calledRun) {
          // @ts-expect-error Module is a global from Emscripten
          dumpDone = Module._eka2l1_frame_dump_done ? Module._eka2l1_frame_dump_done() : 0;
          // @ts-expect-error Module is a global from Emscripten
          dumpCaptured = Module._eka2l1_frame_dump_captured ? Module._eka2l1_frame_dump_captured() : 0;
        }
        return { status, moduleExists, calledRun, hasDumpFn, dumpDone, dumpCaptured };
      });

      log(`status="${state.status}" module=${state.moduleExists} calledRun=${state.calledRun} hasDumpFn=${state.hasDumpFn} captured=${state.dumpCaptured}/8 done=${state.dumpDone}`);

      // Fail fast if emulator never reaches "Running"
      if (!dumpStarted && (performance.now() - runWaitStart > runTimeout)) {
        throw new Error(`Emulator did not reach Running state within ${runTimeout / 1000}s`);
      }

      // Start frame dump once emulator is running and we haven't started yet
      if (!dumpStarted && state.calledRun && state.hasDumpFn && state.status.includes("Running")) {
        log("Starting frame dump...");
        await page.evaluate((dir: string) => {
          try { FS.mkdir(dir); } catch(e) {}
          // @ts-expect-error Module is a global from Emscripten
          Module.ccall('eka2l1_start_frame_dump', null, ['string', 'number'], [dir, 8]);
        }, emFsDir);
        dumpStarted = true;
        log("Frame dump started");
        continue; // re-evaluate state with dumper active
      }

      if (dumpStarted && state.dumpDone) {
        dumpDone = true;
        break;
      }

      if (state.status.includes("Error")) {
        throw new Error(`Emulator error: ${state.status}`);
      }

      await new Promise((r) => setTimeout(r, 2000));
    }

    if (!dumpDone) {
      throw new Error("Frame dump timed out");
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
      log(`  Saved: ${outPath} (${file.data.length} bytes)`);
    }

    log(`Captured ${files.length} frames to ${outDir}`);
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    console.error(`\nFAIL: ${msg}`);
    process.exit(1);
  } finally {
    logStream.end();
    await browser.close();
    server.close();
    releasePidLock();
  }
}

run();
