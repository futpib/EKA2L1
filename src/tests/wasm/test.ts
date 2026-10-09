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
      for (const mode of [-2,1,2,3,8,9,10,11,12,13,14,15,16,18,19]) if (configure(mode) !== -1)
        throw new Error('Retired or invalid compiler policy accepted');
      for (const mode of [0,4,5,6,7,17]) if (configure(mode) !== 0)
        throw new Error('Compiler policy rejected');
      if (configure(-1) !== 0) throw new Error('IR default restoration failed');
      for (const [name, expected, valid, invalid] of [
        ['compiled_svc', 1, [0,1], [-1,2]],
        ['sparse_rom_lookup', 1, [0,1], [-1,2]],
      ] as const) {
        const report = () => m['_eka2l1_' + name + '_report']();
        const configure = (value: number) => m['_eka2l1_' + name + '_configure'](value);
        if (report() !== expected) throw Error('Unexpected build default: ' + name);
        for (const value of valid) if (configure(value) !== 0 || report() !== value)
          throw Error('Policy configuration or readback failed: ' + name);
        for (const value of invalid) if (configure(value) !== -1)
          throw Error('Invalid policy accepted: ' + name);
        if (configure(expected) !== 0 || report() !== expected)
          throw Error('Policy default restoration failed: ' + name);
      }
      for (const name of ['entry_budget','watchdog','division_digits','entry_only_pruning','tlb_hash','memory_cache','rom_dispatch','synchronous_compilation','code_write_protect','code_lookup','omit_guard_publication','compiled_memory_misses','rom_calls','rom_leaves','eager_regions','snakes_n80_native_resolution']) {
        for (const suffix of ['configure','report']) if (typeof m['_eka2l1_' + name + '_' + suffix] !== 'undefined')
          throw Error('Retired configuration API is still exported: ' + name);
      }
      for (const [name, valid, invalid] of [
        ['unsafe_code', [0,3], [-1,1,2,4]],
        ['leaf_features', [0,32,64,96,128,160,192,224], [-1,1,2,4,8,16,65,127,129,159,161,255]],
        ['hotpath', [0,2], [-1,1,3,4,5,6,7,8]],
      ] as const) {
        const previous = m['_eka2l1_' + name + '_report']();
        for (const value of valid) if (m['_eka2l1_' + name + '_configure'](value) !== 0
            || m['_eka2l1_' + name + '_report']() !== value) throw Error('Policy readback failed: ' + name);
        for (const value of invalid) if (m['_eka2l1_' + name + '_configure'](value) !== -1)
          throw Error('Retired policy accepted: ' + name);
        if (m['_eka2l1_' + name + '_configure'](previous) !== 0) throw Error('Policy restoration failed');
      }
    });
    console.log("  PASS");

    console.log("TEST leaf predication: pre-init validation/readback...");
    await page.evaluate(() => {
      const m=(window as any).Module;
      const set=(n:number)=>m.ccall('eka2l1_leaf_predication_configure','number',['number'],[n]);
      if(set(-1)!==-1 || set(2)!==-1 || set(1)!==0 || m.ccall('eka2l1_leaf_predication_report','number',[],[])!==1 || set(0)!==0)
        throw Error('Leaf predication control failed');
    });
    console.log("  PASS");

    console.log("TEST fixed execution limits: readback and retired setter...");
    await page.evaluate(() => {
      const m=(window as any).Module;
      if (typeof m._eka2l1_execution_limits_configure !== 'undefined')
        throw Error('Retired execution limits setter remains exported');
      if (m.ccall('eka2l1_execution_limits_report','string',[],[]) !== '512,32,8,0')
        throw Error('Unexpected fixed execution limits');
    });
    console.log("  PASS");

    console.log("TEST Thumb direct memory: configuration and readback...");
    await page.evaluate(() => {
      const m = (window as any).Module;
      for (const value of [-1, 2]) if (m._eka2l1_thumb_memory_configure(value) !== -1)
        throw Error('Invalid Thumb memory policy accepted');
      for (const value of [1, 0]) if (m._eka2l1_thumb_memory_configure(value) !== 0 || m._eka2l1_thumb_memory_report() !== value)
        throw Error('Thumb memory policy readback failed');
    });
    console.log("  PASS");

    console.log("TEST exact scanner: pre-init policy validation...");
    await page.evaluate(() => {
      const configure = (n: number) => (window as any).Module.ccall('eka2l1_code_compare_configure','number',['number'],[n]);
      if(configure(-1)!==-1 || configure(5)!==-1 || configure(4)!==-1 || configure(3)!==-1 || configure(2)!==0 || configure(1)!==-1 || configure(0)!==0)
        throw Error('Exact comparison policy validation failed');
    });
    console.log("  PASS");

    console.log("TEST custom diagnostics: build capability and opt-in configuration...");
    await page.evaluate(() => {
      const m = (window as any).Module;
      const available = m._eka2l1_diagnostics_available();
      if (available !== 0 && available !== 1) throw Error('Invalid diagnostics capability');
      const expected = available ? 0 : -2;
      for (const name of ['eka2l1_profile_detail_configure', 'eka2l1_guest_profile_configure', 'eka2l1_exit_census_configure']) {
        if (m.ccall(name, 'number', ['number'], [1]) !== expected)
          throw Error(`${name} ignored build capability`);
        if (m.ccall(name, 'number', ['number'], [0]) !== 0)
          throw Error(`${name} could not be disabled`);
      }
      if (m._eka2l1_aot_configure(5, 0, 1) !== -2 || m._eka2l1_aot_configure(0, 0, 0) !== 0)
        throw Error('Crash history ignored build capability');
    });
    console.log("  PASS");

    // Test 5: eka2l1_init
    console.log("TEST 5: eka2l1_init...");
    const initResult = await page.evaluate(() => {
      const m = (window as any).Module;
      if (m._eka2l1_memory_impl_report() !== 2) throw Error('Direct memory is not the WASM default');
      // This API smoke test selected the interpreter above.
      if (m._eka2l1_memory_impl_configure(0) !== 0 || m._eka2l1_memory_impl_report() !== 0)
        throw Error('TLB selection failed');
      // @ts-expect-error Module is a global from Emscripten
      return Module.ccall("eka2l1_init", "number", ["string"], ["/data"]);
    });
    if (initResult !== 0) throw new Error(`eka2l1_init returned ${initResult}`);
    await page.evaluate(() => {
      const m = (window as any).Module;
      for (const name of ['compiled_svc','sparse_rom_lookup']) {
        const before = m['_eka2l1_' + name + '_report']();
        if (m['_eka2l1_' + name + '_configure'](before === 0 ? 1 : 0) !== -1
            || m['_eka2l1_' + name + '_report']() !== before)
          throw Error('Policy changed after initialization: ' + name);
      }
      if ((window as any).Module._eka2l1_thumb_memory_configure(1) !== -1)
        throw Error('Thumb memory policy changed after initialization');
      if ((window as any).Module.ccall('eka2l1_leaf_predication_configure','number',['number'],[1]) !== -1)
        throw Error('Leaf predication changed after initialization');
      if ((window as any).Module.ccall('eka2l1_execution_limits_report','string',[],[]) !== '512,32,8,0')
        throw Error('Fixed execution limits changed after initialization');
      if ((window as any).Module.ccall('eka2l1_ir_configure', 'number', ['number'], [0]) !== -1)
        throw new Error('IR policy changed after initialization');
      if ((window as any).Module.ccall('eka2l1_code_compare_configure','number',['number'],[2]) !== -1)
        throw Error('Exact comparison policy changed after initialization');
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
