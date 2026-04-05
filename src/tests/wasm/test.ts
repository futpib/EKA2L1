import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import puppeteer, { type Page } from "puppeteer";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const buildDir = path.resolve(__dirname, "../../../build-wasm/src/emu/wasm");

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

    // Test 5: eka2l1_init
    console.log("TEST 5: eka2l1_init...");
    const initResult = await page.evaluate(() => {
      // @ts-expect-error Module is a global from Emscripten
      return Module.ccall("eka2l1_init", "number", ["string"], ["/data"]);
    });
    if (initResult !== 0) throw new Error(`eka2l1_init returned ${initResult}`);
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
