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
  console.log(`Fetching ${label} (${cid})...`);
  const chunks: Uint8Array[] = [];
  for await (const chunk of await fetchCid(cid)) {
    chunks.push(chunk);
  }
  const buf = Buffer.concat(chunks);
  fs.writeFileSync(destPath, buf);
  console.log(`  ${label}: ${(buf.length / 1e6).toFixed(1)} MB -> ${destPath}`);
}

interface ConsoleEntry {
  type: string;
  text: string;
}

const PID_FILE = path.join(buildDir, "e2e-test.pid");

function acquirePidLock(): void {
  if (fs.existsSync(PID_FILE)) {
    const oldPid = parseInt(fs.readFileSync(PID_FILE, "utf-8").trim(), 10);
    try {
      process.kill(oldPid, 0); // check if process exists
      console.error(`FAIL: Another e2e test is already running (pid ${oldPid}). Remove ${PID_FILE} if stale.`);
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

async function runTests(): Promise<void> {
  acquirePidLock();

  if (!fs.existsSync(path.join(buildDir, "eka2l1.html"))) {
    console.error("FAIL: build-wasm output not found. Run the WASM build first.");
    releasePidLock();
    process.exit(1);
  }

  // Fetch test data to temp files (Puppeteer uploadFile needs real paths)
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "eka2l1-e2e-"));
  const romFile = path.join(tmpDir, "SYM.ROM");
  const rpkgFile = path.join(tmpDir, "SYM.RPKG");
  const sisFile = path.join(tmpDir, "Snakes.sis");

  await Promise.all([
    fetchCidToFile(ROM_CID, "ROM", romFile),
    fetchCidToFile(RPKG_CID, "RPKG", rpkgFile),
    fetchCidToFile(SIS_CID, "SIS", sisFile),
  ]);

  const { server, port } = await startServer();
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

  // Hook Module before page loads
  await page.evaluateOnNewDocument(() => {
    (window as any).__emscriptenOutput = [];
    (window as any).Module = (window as any).Module || {};
    const origPrintErr = (window as any).Module.printErr;
    (window as any).Module.printErr = function (text: string) {
      (window as any).__emscriptenOutput.push(text);
      console.error(text);
      if (origPrintErr) origPrintErr(text);
    };
    (window as any).Module.onAbort = function (what: any) {
      (window as any).__emscriptenOutput.push("ABORT: " + String(what));
      console.error("ABORT: " + what);
    };
  });

  const consoleMessages: ConsoleEntry[] = [];
  const errors: string[] = [];

  page.on("console", (msg) => {
    const text = msg.text();
    consoleMessages.push({ type: msg.type(), text });
    console.log(`  [${msg.type()}] ${text}`);
  });

  page.on("pageerror", (err) => {
    errors.push(err.message + "\n" + err.stack);
    console.log(`  [pageerror] ${err.message}`);
    if (err.stack) console.log(`  ${err.stack}`);
  });

  page.on("error", (err) => {
    console.log(`  [page crash] ${err.message}`);
  });

  let exitCode = 0;
  const t0 = performance.now();
  function log(msg: string): void {
    const sec = ((performance.now() - t0) / 1000).toFixed(1);
    console.log(`[${sec}s] ${msg}`);
  }

  try {
    // 1. Load page, wait for WASM
    log("Loading WASM module...");
    await page.goto(url, { waitUntil: "networkidle0", timeout: 60_000 });
    await page.waitForFunction(
      "typeof Module._eka2l1_init === 'function'",
      { timeout: 120_000 },
    );
    log("PASS: WASM module loaded\n");

    // 2. Upload files via file inputs (like a user would)
    log("Selecting ROM file...");
    const romInput = await page.waitForSelector("#rom-file");
    await romInput!.uploadFile(romFile);
    log("PASS: ROM selected\n");

    log("Selecting RPKG file...");
    const rpkgInput = await page.waitForSelector("#rpkg-file");
    await rpkgInput!.uploadFile(rpkgFile);
    log("PASS: RPKG selected\n");

    log("Selecting SIS file...");
    const sisInput = await page.waitForSelector("#sis-file");
    await sisInput!.uploadFile(sisFile);
    log("PASS: SIS selected\n");

    // 3. Type app name
    log("Typing app name...");
    await page.type("#app-name", "Snakes");
    log("PASS: App name entered\n");

    // 4. Click Start
    log("Clicking Start...");
    await page.click("#btn-start");
    log("Start clicked — waiting for emulator...\n");

    // 5. Wait for the emulator to start running.
    // startEmulator() blocks the main thread during ccalls, then sets status
    // to "Running: ..." and starts emscripten_set_main_loop. The main loop
    // (symsys->loop()) then consumes the main thread. We poll for status
    // change OR console output indicating the emulator started.
    log("Waiting for emulator to start...");
    await page.waitForFunction(
      () => {
        const status = document.getElementById("status")?.textContent ?? "";
        if (status.includes("Error")) return true;
        if (status.includes("Running")) return true;
        // Also check if button is disabled (startEmulator running/done)
        const btn = document.getElementById("btn-start") as HTMLButtonElement;
        return btn?.disabled === true && !status.includes("Initializing") && !status.includes("Uploading") && !status.includes("Installing");
      },
      { timeout: 300_000, polling: "raf" },
    );

    const status = await page.$eval("#status", (el) => el.textContent ?? "");
    log(`Status: "${status}"`);

    if (status.includes("Error")) {
      throw new Error(`Emulator error: ${status}`);
    }

    log("PASS: Emulator is running\n");

    // 6. Wait for meaningful frame — canvas has non-zero pixels.
    // Poll from Node so we can also check for page errors / aborts.
    log("Waiting for meaningful frame...");
    const pixelTimeout = 120_000;
    const pixelStart = performance.now();
    let gotPixels = false;
    while (performance.now() - pixelStart < pixelTimeout) {
      if (errors.length > 0) {
        throw new Error(`Page error while waiting for frame: ${errors[0]}`);
      }
      const abortMsg = consoleMessages.find((m) => m.text.includes("ABORT:"));
      if (abortMsg) {
        throw new Error(`WASM abort while waiting for frame: ${abortMsg.text}`);
      }
      const pixelInfo = await page.evaluate(() => {
        const canvas = document.getElementById("canvas") as HTMLCanvasElement;
        if (!canvas || canvas.width === 0 || canvas.height === 0) return { ok: false, debug: "no canvas", dataUrl: "" };
        const dataUrl = canvas.toDataURL("image/png");
        const tmp = document.createElement("canvas");
        tmp.width = canvas.width;
        tmp.height = canvas.height;
        const ctx = tmp.getContext("2d");
        let nonBlack = 0;
        if (ctx) {
          ctx.drawImage(canvas, 0, 0);
          const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
          for (let i = 0; i < data.length; i += 4) {
            if (data[i] !== 0 || data[i + 1] !== 0 || data[i + 2] !== 0) nonBlack++;
          }
        }
        return { ok: nonBlack > 10, debug: `w=${canvas.width} h=${canvas.height} nonBlack=${nonBlack} dataUrlLen=${dataUrl.length}`, dataUrl };
      });
      // Save screenshot each check
      if (pixelInfo.dataUrl) {
        const pngData = Buffer.from(pixelInfo.dataUrl.replace(/^data:image\/png;base64,/, ""), "base64");
        fs.writeFileSync(path.resolve(buildDir, "../../../e2e-screenshot.png"), pngData);
      }
      if (!gotPixels) {
        const elapsed = performance.now() - pixelStart;
        if (elapsed < 2000 || Math.floor(elapsed / 10000) !== Math.floor((elapsed - 500) / 10000)) {
          log(`  pixel check: ${pixelInfo.debug}`);
        }
      }
      gotPixels = pixelInfo.ok;
      if (gotPixels) break;
      await new Promise((r) => setTimeout(r, 500));
    }
    if (!gotPixels) {
      throw new Error(`No meaningful pixels after ${(pixelTimeout / 1000).toFixed(0)}s`);
    }
    log("PASS: Canvas has meaningful pixels\n");

    // 7. Wait for actual app content — read pixels directly from GL framebuffer
    // via eka2l1_read_screen_pixels (bypasses OFFSCREEN_FRAMEBUFFER canvas compositing issues)
    log("Waiting for app content (GL readback)...");
    const contentTimeout = 120_000;
    const contentStart = performance.now();
    let gotContent = false;
    while (performance.now() - contentStart < contentTimeout) {
      if (errors.length > 0) {
        throw new Error(`Page error while waiting for content: ${errors[0]}`);
      }
      // Read distinct color count from the gfx thread's last frame readback
      const distinctColors = await page.evaluate(() => {
        // @ts-expect-error Module is a global from Emscripten
        return Module._eka2l1_get_distinct_colors ? Module._eka2l1_get_distinct_colors() : 0;
      });
      const elapsed = performance.now() - contentStart;
      const totalElapsed = Math.round((performance.now() - t0) / 1000);
      if (elapsed < 2000 || Math.floor(elapsed / 10000) !== Math.floor((elapsed - 1000) / 10000)) {
        log(`  content check: distinctColors=${distinctColors}`);
        // Save periodic screenshot every 10 seconds
        const el = await page.$("#canvas");
        if (el) {
          const buf = await el.screenshot({ type: "png" });
          fs.writeFileSync(path.resolve(buildDir, `../../../e2e-screenshot-${totalElapsed}s.png`), buf);
        }
      }
      if (distinctColors > 3) {
        gotContent = true;
        // Save screenshot immediately when content detected
        const el = await page.$("#canvas");
        if (el) {
          const buf = await el.screenshot({ type: "png" });
          fs.writeFileSync(path.resolve(buildDir, `../../../e2e-screenshot-content.png`), buf);
        }
        break;
      }
      // Send periodic key presses to interact with the app (e.g. dismiss menus)
      const elapsedContent = performance.now() - contentStart;
      const keySequence = [
        290, // F1 = left softkey
        257, // Enter = select/OK
        325, // Numpad 5 = center/select
        265, // Up arrow
        264, // Down arrow
        257, // Enter
      ];
      if (Math.floor(elapsedContent / 3000) !== Math.floor((elapsedContent - 1000) / 3000)) {
        const keyIndex = Math.floor(elapsedContent / 3000) % keySequence.length;
        await page.evaluate((key: number) => {
          // @ts-expect-error Module is a global from Emscripten
          if (Module._eka2l1_press_key) Module._eka2l1_press_key(key);
        }, keySequence[keyIndex]);
      }
      await new Promise((r) => setTimeout(r, 1000));
    }

    // Save screenshot with timestamp
    const saveScreenshot = async (label: string) => {
      const el = await page.$("#canvas");
      if (el) {
        const buf = await el.screenshot({ type: "png" });
        const name = `e2e-screenshot-${label}.png`;
        fs.writeFileSync(path.resolve(buildDir, "../../../" + name), buf);
        log(`  screenshot: ${name} (${buf.length} bytes)`);
      }
    };

    await saveScreenshot(`${Math.round((performance.now() - t0) / 1000)}s`);

    if (!gotContent) {
      throw new Error(`No app content after ${(contentTimeout / 1000).toFixed(0)}s`);
    }
    log("PASS: Canvas has diverse app content\n");

    log("All e2e tests passed!");
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    console.error(`\nFAIL: ${msg}`);
    exitCode = 1;

    console.log("\nBrowser console (last 30):");
    for (const m of consoleMessages.slice(-30)) {
      console.log(`  [${m.type}] ${m.text}`);
    }
    if (errors.length > 0) {
      console.log("\nPage errors:");
      for (const e of errors) console.log(`  ${e}`);
    }
  } finally {
    await browser.close();
    server.close();

    // Cleanup temp files
    fs.rmSync(tmpDir, { recursive: true, force: true });

    releasePidLock();
  }

  process.exit(exitCode);
}

runTests();
