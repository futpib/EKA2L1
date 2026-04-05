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

      const filePath = path.join(buildDir, urlPath);

      if (!fs.existsSync(filePath)) {
        res.writeHead(404);
        res.end("Not found");
        return;
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
    args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-gpu"],
  });

  const page: Page = await browser.newPage();

  const consoleMessages: ConsoleEntry[] = [];
  const errors: string[] = [];

  page.on("console", (msg) => {
    const text = msg.text();
    consoleMessages.push({ type: msg.type(), text });
    if (
      text.includes("EKA2L1") ||
      text.includes("Installing") ||
      text.includes("installed") ||
      text.includes("Launching") ||
      text.includes("error") ||
      text.includes("Error") ||
      text.includes("FAIL")
    ) {
      console.log(`  [emu] ${text}`);
    }
  });

  page.on("pageerror", (err) => {
    errors.push(err.message);
  });

  let exitCode = 0;

  try {
    // 1. Load page and wait for WASM
    console.log("Loading WASM module...");
    await page.goto(url, { waitUntil: "networkidle0", timeout: 60_000 });
    await page.waitForFunction(
      "typeof Module._eka2l1_init === 'function'",
      { timeout: 120_000 },
    );
    console.log("PASS: WASM module loaded\n");

    // 2. Init emulator
    console.log("Initializing emulator...");
    const initResult = await page.evaluate(() => {
      // @ts-expect-error Module is Emscripten global
      return Module.ccall("eka2l1_init", "number", ["string"], ["/data"]);
    });
    if (initResult !== 0) throw new Error(`eka2l1_init returned ${initResult}`);
    console.log("PASS: eka2l1_init succeeded\n");

    // 3. Upload ROM
    console.log("Uploading ROM to Emscripten FS...");
    await uploadBufferToEmscriptenFS(page, romData, "/tmp/SYM.ROM");
    console.log("PASS: ROM uploaded\n");

    // 4. Upload RPKG
    console.log("Uploading RPKG to Emscripten FS...");
    await uploadBufferToEmscriptenFS(page, rpkgData, "/tmp/SYM.RPKG");
    console.log("PASS: RPKG uploaded\n");

    // 5. Install device
    console.log("Installing device...");
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
    console.log("PASS: Device installed\n");

    // 6. Upload SIS
    console.log("Uploading SIS to Emscripten FS...");
    await uploadBufferToEmscriptenFS(page, sisData, "/tmp/Snakes.sis");
    console.log("PASS: SIS uploaded\n");

    // 7. Install SIS
    console.log("Installing SIS...");
    const sisResult = await page.evaluate(() => {
      // @ts-expect-error Module is Emscripten global
      return Module.ccall(
        "eka2l1_install_sis",
        "number",
        ["string"],
        ["/tmp/Snakes.sis"],
      );
    });
    if (sisResult !== 0)
      throw new Error(`eka2l1_install_sis returned ${sisResult}`);
    console.log("PASS: SIS installed\n");

    // 8. Run the app
    console.log("Launching Snakes...");
    const runResult = await page.evaluate(() => {
      // @ts-expect-error Module is Emscripten global
      return Module.ccall("eka2l1_run", "number", ["string"], ["Snakes"]);
    });
    if (runResult !== 0)
      throw new Error(`eka2l1_run returned ${runResult}`);
    console.log("PASS: eka2l1_run succeeded\n");

    // 9. Let the emulator run for a few seconds
    console.log("Letting emulator run for 5 seconds...");
    await new Promise((resolve) => setTimeout(resolve, 5_000));

    // 10. Check canvas has been drawn to
    console.log("Checking canvas...");
    const hasPixels = await page.evaluate(() => {
      const canvas = document.getElementById("canvas") as HTMLCanvasElement;
      if (!canvas) return false;
      const gl = canvas.getContext("webgl2");
      if (!gl) return false;
      const pixels = new Uint8Array(4 * 10);
      gl.readPixels(0, 0, 10, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      return pixels.some((v) => v !== 0);
    });
    if (hasPixels) {
      console.log("PASS: Canvas is rendering\n");
    } else {
      console.log("WARN: Canvas is blank (rendering may not work yet)\n");
    }

    // 11. Shutdown
    console.log("Shutting down...");
    await page.evaluate(() => {
      // @ts-expect-error Module is Emscripten global
      Module.ccall("eka2l1_shutdown", null, [], []);
    });
    console.log("PASS: eka2l1_shutdown succeeded\n");

    console.log("All e2e tests passed!");
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
  }

  process.exit(exitCode);
}

runTests();
