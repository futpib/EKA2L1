import fs from "node:fs";
import path from "node:path";
import { buildDir, startServer } from "./server.ts";

if (!fs.existsSync(path.join(buildDir, "eka2l1.html"))) {
  console.error("build-wasm output not found. Run the WASM build first.");
  process.exit(1);
}

const port = parseInt(process.argv[2] ?? "8080", 10);

const { port: resolvedPort } = await startServer(port);
console.log(`Serving EKA2L1 WASM at http://127.0.0.1:${resolvedPort}/`);
console.log(`Build dir: ${buildDir}`);
console.log("Press Ctrl+C to stop.");
