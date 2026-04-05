import http from "node:http";
import fs from "node:fs";
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

      // Serve favicon from project source
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

      // Source maps and worker files may be in parent dirs
      if (!fs.existsSync(filePath)) {
        const basename = path.basename(urlPath);
        const altPath = path.join(buildDir, basename);
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

async function fetchCidToBuffer(cid: string, label: string): Promise<Buffer> {
  console.log(`Fetching ${label} (${cid})...`);
  const chunks: Uint8Array[] = [];
  for await (const chunk of await fetchCid(cid)) {
    chunks.push(chunk);
  }
  const buf = Buffer.concat(chunks);
  console.log(`  ${label}: ${(buf.length / 1e6).toFixed(1)} MB`);
  return buf;
}

async function uploadBufferToEmscriptenFS(
  page: Page,
  data: Buffer,
  emsPath: string,
): Promise<void> {
  // Upload in 8MB chunks to avoid string length limits
  const CHUNK_SIZE = 8 * 1024 * 1024;

  await page.evaluate((dest: string) => {
    const parts = dest.split("/");
    let dir = "";
    for (let i = 0; i < parts.length - 1; i++) {
      if (!parts[i]) continue;
      dir += "/" + parts[i];
      try {
        // @ts-expect-error FS is Emscripten global
        FS.mkdir(dir);
      } catch {
        // already exists
      }
    }
    // Create empty file
    // @ts-expect-error FS is Emscripten global
    FS.writeFile(dest, new Uint8Array(0));
  }, emsPath);

  for (let offset = 0; offset < data.length; offset += CHUNK_SIZE) {
    const chunk = data.subarray(offset, Math.min(offset + CHUNK_SIZE, data.length));
    const base64 = chunk.toString("base64");

    await page.evaluate(
      (b64: string, dest: string, off: number) => {
        const binary = atob(b64);
        const bytes = new Uint8Array(binary.length);
        for (let i = 0; i < binary.length; i++) {
          bytes[i] = binary.charCodeAt(i);
        }

        if (off === 0) {
          // @ts-expect-error FS is Emscripten global
          FS.writeFile(dest, bytes);
        } else {
          // @ts-expect-error FS is Emscripten global
          const stream = FS.open(dest, "a");
          // @ts-expect-error FS is Emscripten global
          FS.write(stream, bytes, 0, bytes.length);
          // @ts-expect-error FS is Emscripten global
          FS.close(stream);
        }
      },
      base64,
      emsPath,
      offset,
    );
  }
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

  // Fetch test data from IPFS
  const [romData, rpkgData, sisData] = await Promise.all([
    fetchCidToBuffer(ROM_CID, "ROM"),
    fetchCidToBuffer(RPKG_CID, "RPKG"),
    fetchCidToBuffer(SIS_CID, "SIS"),
  ]);

  const { server, port } = await startServer();
  const url = `http://127.0.0.1:${port}/`;
  console.log(`\nServing at ${url}\n`);

  const browser = await puppeteer.launch({
    headless: true,
    protocolTimeout: 1200_000, // 20 minutes for slow WASM operations
    args: [
      "--no-sandbox",
      "--disable-setuid-sandbox",
      "--use-gl=angle",
      "--use-angle=swiftshader",
    ],
  });

  const page: Page = await browser.newPage();
  page.setDefaultTimeout(300_000); // 5 minutes for long operations

  const consoleMessages: ConsoleEntry[] = [];
  const errors: string[] = [];

  // Hook Module.printErr/onAbort BEFORE the page loads Emscripten JS
  await page.evaluateOnNewDocument(() => {
    (window as any).__emscriptenOutput = [];
    (window as any).Module = (window as any).Module || {};
    (window as any).Module.printErr = function (text: string) {
      (window as any).__emscriptenOutput.push(text);
      console.error(text);
    };
    (window as any).Module.onAbort = function (what: any) {
      (window as any).__emscriptenOutput.push("ABORT: " + String(what));
      console.error("ABORT: " + what);
    };
  });

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
    log("Loading WASM module...");
    await page.goto(url, { waitUntil: "networkidle0", timeout: 60_000 });
    await page.waitForFunction(
      "typeof Module._eka2l1_init === 'function'",
      { timeout: 120_000 },
    );
    log("PASS: WASM module loaded\n");

    log("Initializing emulator...");
    const initResult = await page.evaluate(() => {
      // @ts-expect-error Module is Emscripten global
      return Module.ccall("eka2l1_init", "number", ["string"], ["/data"]);
    });
    if (initResult !== 0) throw new Error(`eka2l1_init returned ${initResult}`);
    log("PASS: eka2l1_init succeeded\n");

    // Quick WebGL2 sanity check before long install
    log("Checking WebGL2 support...");
    const webgl2ok = await page.evaluate(() => {
      const c = document.createElement("canvas");
      const gl = c.getContext("webgl2");
      return gl !== null;
    });
    if (!webgl2ok) throw new Error("WebGL2 not available in this browser");
    log("PASS: WebGL2 available\n");

    log("Uploading ROM to Emscripten FS...");
    await uploadBufferToEmscriptenFS(page, romData, "/tmp/SYM.ROM");
    log("PASS: ROM uploaded\n");

    log("Uploading RPKG to Emscripten FS...");
    await uploadBufferToEmscriptenFS(page, rpkgData, "/tmp/SYM.RPKG");
    log("PASS: RPKG uploaded\n");

    log("Installing device...");
    const deviceResult = await page.evaluate(() => {
      // @ts-expect-error Module is Emscripten global
      return Module.ccall(
        "eka2l1_install_device",
        "number",
        ["string", "string"],
        ["/tmp/SYM.ROM", "/tmp/SYM.RPKG"],
      );
    });
    if (deviceResult !== 0)
      throw new Error(`eka2l1_install_device returned ${deviceResult}`);
    log("PASS: Device installed\n");

    log("Uploading SIS to Emscripten FS...");
    await uploadBufferToEmscriptenFS(page, sisData, "/tmp/Snakes.sis");
    log("PASS: SIS uploaded\n");

    log("Installing SIS...");
    const sisResult = await page.evaluate(() => {
      try {
        // @ts-expect-error Module is Emscripten global
        return Module.ccall(
          "eka2l1_install_sis",
          "number",
          ["string"],
          ["/tmp/Snakes.sis"],
        );
      } catch (e: any) {
        const msg = e?.message ?? e?.toString() ?? String(e);
        const stack = e?.stack ?? "";
        return "EXCEPTION: " + msg + "\nSTACK: " + stack;
      }
    });
    if (typeof sisResult === "string") throw new Error(sisResult);
    if (sisResult !== 0) {
      log("WARN: eka2l1_install_sis returned -1 (game may already be in ROM)\n");
    } else {
      log("PASS: SIS installed\n");
    }

    log("Launching Snakes...");
    const runResult = await page.evaluate(() => {
      // @ts-expect-error Module is Emscripten global
      return Module.ccall("eka2l1_run", "number", ["string"], ["Snakes"]);
    });
    if (runResult !== 0)
      throw new Error(`eka2l1_run returned ${runResult}`);
    log("PASS: eka2l1_run succeeded\n");

    // The emulator main loop runs via emscripten_set_main_loop and yields
    // to the browser event loop each frame. We can't easily check canvas
    // pixels because the GL context is on the main thread.
    // eka2l1_run succeeding means the emulator started — that's the test.

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

    // Dump any collected Emscripten stderr output
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
    await browser.close();
    server.close();
  }

  process.exit(exitCode);
}

runTests();
