import http from "node:http";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import puppeteer, { type Page } from "puppeteer";
import { fetchCid } from "@futpib/fetch-cid";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const buildDir = path.resolve(__dirname, "../../../build-wasm/src/emu/wasm");

const ROM_CID = "bafybeicj2jkrjfirzdz5jezz6hjbx2ylyv343kaecnhytl3g6yjy3mwmqm";
const RPKG_CID = "bafybeihjy4vjxb5cy7zxca4kedg5ncxf5xrbj5basirfefemqwru73aipu";
const SIS_CID = "bafybeicuomcc2zhzi3vwfb5xihnlkikz3biaa43g4d22z2wptcmhvmp3di";

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

function startServer(): Promise<{ server: http.Server; port: number }> {
  return new Promise((resolve) => {
    const server = http.createServer((req, res) => {
      let urlPath = (req.url ?? "/").split("?")[0];
      if (urlPath === "/") urlPath = "/eka2l1.html";

      if (urlPath === "/favicon.ico") {
        const icoPath = path.resolve(__dirname, "../../emu/qt/duck_tank.ico");
        if (fs.existsSync(icoPath)) {
          res.writeHead(200, { "Content-Type": "image/x-icon" });
          fs.createReadStream(icoPath).pipe(res);
        } else {
          res.writeHead(204);
          res.end();
        }
        return;
      }

      let filePath = path.join(buildDir, urlPath);

      if (!fs.existsSync(filePath)) {
        const altPath = path.join(buildDir, path.basename(urlPath));
        if (fs.existsSync(altPath)) {
          filePath = altPath;
        } else {
          console.log(`  [server] 404: ${urlPath}`);
          res.writeHead(404);
          res.end("Not found");
          return;
        }
      }

      res.writeHead(200, {
        "Content-Type": getMime(filePath),
        "Cross-Origin-Opener-Policy": "same-origin",
        "Cross-Origin-Embedder-Policy": "require-corp",
      });
      fs.createReadStream(filePath).pipe(res);
    });

    server.listen(0, "127.0.0.1", () => {
      const addr = server.address();
      const port = typeof addr === "object" && addr ? addr.port : 0;
      resolve({ server, port });
    });
  });
}

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

async function runTests(): Promise<void> {
  if (!fs.existsSync(path.join(buildDir, "eka2l1.html"))) {
    console.error("FAIL: build-wasm output not found. Run the WASM build first.");
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
    // The emulator main loop may consume the main thread, so we use
    // raf polling which piggybacks on requestAnimationFrame.
    log("Waiting for meaningful frame...");
    await page.waitForFunction(
      () => {
        const canvas = document.getElementById("canvas") as HTMLCanvasElement;
        if (!canvas) return false;
        const gl = canvas.getContext("webgl2");
        if (!gl) return false;
        const pixels = new Uint8Array(canvas.width * 4);
        gl.readPixels(0, canvas.height / 2, canvas.width, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        let nonZero = 0;
        for (let i = 0; i < pixels.length; i += 4) {
          if (pixels[i] !== 0 || pixels[i + 1] !== 0 || pixels[i + 2] !== 0) {
            nonZero++;
          }
        }
        return nonZero > 10;
      },
      { timeout: 60_000, polling: "raf" },
    );
    log("PASS: Canvas has meaningful pixels\n");

    log("All e2e tests passed!");
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    console.error(`\nFAIL: ${msg}`);
    exitCode = 1;

    // Flush pending logs before reporting
    try {
      await page.evaluate(() => (window as any)._flushLog?.());
      await new Promise((r) => setTimeout(r, 500));
    } catch { /* page may be unresponsive */ }

    console.log("\nBrowser console (last 30):");
    for (const m of consoleMessages.slice(-30)) {
      console.log(`  [${m.type}] ${m.text}`);
    }
    if (errors.length > 0) {
      console.log("\nPage errors:");
      for (const e of errors) console.log(`  ${e}`);
    }

    try {
      const emsOutput: string[] = await page.evaluate(
        () => (window as any).__emscriptenOutput ?? [],
      );
      if (emsOutput.length > 0) {
        console.log("\nEmscripten stderr:");
        for (const line of emsOutput) console.log(`  ${line}`);
      }
    } catch {
      // page may be closed
    }
  } finally {
    // Flush any remaining logs
    try {
      await page.evaluate(() => (window as any)._flushLog?.());
      await new Promise((r) => setTimeout(r, 500));
    } catch { /* ignore */ }

    await browser.close();
    server.close();

    // Cleanup temp files
    try {
      fs.rmSync(tmpDir, { recursive: true });
    } catch {
      // ignore
    }
  }

  process.exit(exitCode);
}

runTests();
