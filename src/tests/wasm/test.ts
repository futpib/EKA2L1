import http from "node:http";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import puppeteer, { type Page } from "puppeteer";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const buildDir = path.resolve(process.env.EKA2L1_WASM_BUILD_DIR || path.join(__dirname, "../../../build-wasm/src/emu/wasm"));

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

interface ConsoleEntry {
  type: string;
  text: string;
}

async function runTests(): Promise<void> {
  if (!fs.existsSync(path.join(buildDir, "eka2l1.html"))) {
    console.error("FAIL: build-wasm output not found. Run the WASM build first.");
    process.exit(1);
  }

  console.log(`Build: ${buildDir}`);
  console.log(`WASM SHA-256: ${crypto.createHash("sha256").update(fs.readFileSync(path.join(buildDir, "eka2l1.wasm"))).digest("hex")}`);
  const { server, port } = await startServer();
  const url = `http://127.0.0.1:${port}/`;
  console.log(`Serving WASM build at ${url}`);

  const browser = await puppeteer.launch({
    headless: true,
    args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-gpu"],
  });

  const page: Page = await browser.newPage();

  const consoleMessages: ConsoleEntry[] = [];
  const errors: string[] = [];

  page.on("console", (msg) => {
    consoleMessages.push({ type: msg.type(), text: msg.text() });
  });

  page.on("pageerror", (err) => {
    errors.push(err.message);
  });

  let exitCode = 0;

  try {
    // Test 1: Page loads
    console.log("TEST 1: Page loads...");
    await page.goto(url, { waitUntil: "networkidle0", timeout: 60_000 });
    console.log("  PASS");

    // Test 2: Module object exists
    console.log("TEST 2: WASM Module object...");
    await page.waitForFunction("typeof Module !== 'undefined'", {
      timeout: 30_000,
    });
    console.log("  PASS");

    // Test 3: Canvas exists
    console.log("TEST 3: Canvas element...");
    const canvas = await page.$("#canvas");
    if (!canvas) throw new Error("Canvas element not found");
    console.log("  PASS");

    // Test 4: Exported C functions callable
    console.log("TEST 4: Exported C functions...");
    await page.waitForFunction(
      "typeof Module._eka2l1_init === 'function'",
      { timeout: 60_000 },
    );
    console.log("  PASS");

    console.log("TEST IR: pre-init compiler mode validation...");
    await page.evaluate(() => {
      const m = (window as any).Module;
      const configure = (n: number) => m.ccall('eka2l1_ir_configure', 'number', ['number'], [n]);
      if (configure(-2) !== -1 || configure(16) !== -1 || configure(0) !== 0)
        throw new Error('IR mode validation failed');
      for (const mode of [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]) if (![0,-2].includes(configure(mode)))
        throw new Error('IR capability response failed');
      if (configure(-1) !== 0) throw new Error('IR default restoration failed');
    });
    console.log("  PASS");

    console.log("TEST eager ROM: pre-init mode validation...");
    await page.evaluate(() => {
      const configure = (n: number) => (window as any).Module.ccall('eka2l1_eager_regions_configure', 'number', ['number'], [n]);
      if (configure(-1) !== -1 || configure(2) !== -1 || configure(1) !== 0 || configure(0) !== 0)
        throw new Error('Eager ROM mode validation failed');
    });
    console.log("  PASS");

    console.log("TEST lookup layout: pre-init policy validation...");
    await page.evaluate(() => {
      const configure = (n: number) => (window as any).Module.ccall('eka2l1_code_lookup_configure','number',['number'],[n]);
      if(configure(-1)!==-1 || configure(2)!==-1 || configure(1)!==0 || configure(0)!==0)
        throw Error('Code lookup policy validation failed');
    });
    console.log("  PASS");

    console.log("TEST code write protection: pre-init capability and policy validation...");
    await page.evaluate((expectEnabled) => {
      const configure = (n: number) => (window as any).Module.ccall('eka2l1_code_write_protect_configure','number',['number'],[n]);
      if(configure(-1)!==-1 || configure(2)!==-1) throw Error('Invalid write protection accepted');
      const enabled=configure(1);
      if(enabled===0) {
        const ir=(n: number)=>(window as any).Module.ccall('eka2l1_ir_configure','number',['number'],[n]);
        if(ir(7)!==0 || configure(0)!==-2 || ir(-1)!==0) throw Error('Protected proof policy requirements not enforced');
      }
      const disabled=configure(0);
      if(expectEnabled ? (enabled!==0 || disabled!==0) : !((enabled===0 && disabled===0)||(enabled===-1 && disabled===-1)))
        throw Error('Write protection capability mismatch');
    }, process.env.EKA2L1_EXPECT_WRITE_PROTECTION==='1');
    console.log("  PASS");

    console.log("TEST exact scanner: pre-init policy validation...");
    await page.evaluate(() => {
      const configure = (n: number) => (window as any).Module.ccall('eka2l1_code_compare_configure','number',['number'],[n]);
      if(configure(-1)!==-1 || configure(3)!==-1 || configure(2)!==0 || configure(1)!==0 || configure(0)!==0)
        throw Error('Exact comparison policy validation failed');
    });
    console.log("  PASS");

    console.log("TEST TLB hash: pre-init policy validation...");
    await page.evaluate(() => {
      const configure = (n: number) => (window as any).Module.ccall('eka2l1_tlb_hash_configure','number',['number'],[n]);
      if(configure(-1)!==-1 || configure(2)!==-1 || configure(1)!==0 || configure(0)!==0)
        throw Error('TLB index policy validation failed');
    });
    console.log("  PASS");

    // Test 5: eka2l1_init
    console.log("TEST 5: eka2l1_init...");
    const initResult = await page.evaluate(() => {
      // @ts-expect-error Module is a global from Emscripten
      return Module.ccall("eka2l1_init", "number", ["string"], ["/data"]);
    });
    if (initResult !== 0) throw new Error(`eka2l1_init returned ${initResult}`);
    await page.evaluate(() => {
      if ((window as any).Module.ccall('eka2l1_code_write_protect_configure','number',['number'],[1]) !== -1)
        throw Error('Write protection changed after initialization');
      if ((window as any).Module.ccall('eka2l1_code_lookup_configure','number',['number'],[1]) !== -1)
        throw Error('Code lookup policy changed after initialization');
      if ((window as any).Module.ccall('eka2l1_tlb_hash_configure','number',['number'],[1]) !== -1)
        throw Error('TLB index policy changed after initialization');
      if ((window as any).Module.ccall('eka2l1_ir_configure', 'number', ['number'], [0]) !== -1)
        throw new Error('IR policy changed after initialization');
      if ((window as any).Module.ccall('eka2l1_code_compare_configure','number',['number'],[1]) !== -1)
        throw Error('Exact comparison policy changed after initialization');
      if ((window as any).Module.ccall('eka2l1_eager_regions_configure', 'number', ['number'], [1]) !== -1)
        throw new Error('Eager ROM policy changed after initialization');
    });
    console.log("  PASS");

    // Test 6: eka2l1_shutdown
    console.log("TEST 6: eka2l1_shutdown...");
    await page.evaluate(() => {
      // @ts-expect-error Module is a global from Emscripten
      Module.ccall("eka2l1_shutdown", null, [], []);
    });
    console.log("  PASS");

    // Test 7: No fatal page errors
    console.log("TEST 7: No fatal page errors...");
    const fatalErrors = errors.filter(
      (e) => !e.includes("SharedArrayBuffer") && !e.includes("pthread"),
    );
    if (fatalErrors.length > 0) {
      throw new Error(`Got ${fatalErrors.length} page error(s): ${fatalErrors[0]}`);
    }
    console.log("  PASS");

    console.log("\nAll tests passed!");
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    console.error(`\nFAIL: ${msg}`);
    exitCode = 1;

    if (consoleMessages.length > 0) {
      console.log("\nBrowser console (last 20):");
      for (const m of consoleMessages.slice(-20)) {
        console.log(`  [${m.type}] ${m.text}`);
      }
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
