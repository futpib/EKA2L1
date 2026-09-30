import http from "node:http";
import https from "node:https";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export const buildDir = process.env.EKA2L1_WASM_BUILD_DIR
  ? path.resolve(process.env.EKA2L1_WASM_BUILD_DIR)
  : path.resolve(__dirname, "../../../build-wasm/src/emu/wasm");

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

function makeAutoStartScript(appName?: string): string {
  if (!appName) return "";
  const encodedName = JSON.stringify(appName).replace(/</g, "\\u003c");
  return `
<script>
async function autoStart() {
  while (typeof Module === 'undefined' || !Module.calledRun)
    await new Promise(resolve => setTimeout(resolve, 100));
  try {
    // Use the same live configuration, graphics handshake and input activation
    // as a manual Start. Keep only preload-file selection in this helper.
    for (const [endpoint, selector, name] of [
      ['/preload/rom', 'rom-file', 'SYM.ROM'],
      ['/preload/rpkg', 'rpkg-file', 'SYM.RPKG'],
      ['/preload/sis', 'sis-file', 'app.sis']]) {
      const response = await fetch(endpoint);
      if (response.status === 404) continue;
      if (!response.ok) throw new Error(endpoint + ': HTTP ' + response.status);
      const transfer = new DataTransfer();
      transfer.items.add(new File([await response.blob()], name));
      document.getElementById(selector).files = transfer.files;
    }
    document.getElementById('app-name').value = ${encodedName};
    await startEmulator();
  } catch (error) {
    Module.setStatus('Error: ' + error.message);
    Module.printErr(error.toString());
  }
}
autoStart();
</script>`;
}

export type CompilerPolicy = { irMode?: number; eagerRegions?: number };

export function compilerPolicyFromEnv(): CompilerPolicy | undefined {
  const ir = process.env.EKA2L1_AOT_IR_MODE;
  const eager = process.env.EKA2L1_AOT_EAGER_REGIONS;
  if (ir === undefined && eager === undefined) return undefined;
  const policy: CompilerPolicy = {};
  if (ir !== undefined) {
    if (!/^(?:-1|[0-4])$/.test(ir)) throw new Error("Invalid compiler policy");
    policy.irMode = Number(ir);
  }
  if (eager !== undefined) {
    if (!/^[01]$/.test(eager)) throw new Error("Invalid eager-region policy");
    policy.eagerRegions = Number(eager);
  }
  return policy;
}

function makeCompilerPolicyScript(policy?: CompilerPolicy): string {
  if (!policy) return "";
  if ((policy.irMode !== undefined && (!Number.isInteger(policy.irMode) || policy.irMode < -1 || policy.irMode > 4))
      || (policy.eagerRegions !== undefined && ![0,1].includes(policy.eagerRegions)))
    throw new Error("Invalid compiler policy");
  return `<script>
window.ekaCompilerPolicy = {requested:${JSON.stringify(policy)}, applied:false};
{
  const originalStart = startEmulator;
  startEmulator = async function() {
    const state = window.ekaCompilerPolicy;
    if (!state.applied) {
      for (const [key, entry] of [['irMode','eka2l1_ir_configure'], ['eagerRegions','eka2l1_eager_regions_configure']]) {
        if (state.requested[key] === undefined) continue;
        if (typeof Module['_' + entry] !== 'function'
            || Module.ccall(entry, 'number', ['number'], [state.requested[key]]) !== 0)
          throw new Error('Emulator compiler configuration failed: ' + key);
      }
      state.applied = true;
    }
    return originalStart();
  };
}
</script>`;
}

export function startServer(
  port = 0,
  preloadFiles: Record<string, string> = {},
  appName?: string,
  options: { host?: string; tls?: https.ServerOptions; compilerPolicy?: CompilerPolicy } = {},
): Promise<{ server: http.Server; port: number }> {
  return new Promise((resolve, reject) => {
    const autoStartScript = makeCompilerPolicyScript(options.compilerPolicy) + makeAutoStartScript(appName);

    const handler: http.RequestListener = (req, res) => {
      let urlPath = (req.url ?? "/").split("?")[0];
      if (urlPath === "/") urlPath = "/eka2l1.html";

      // Serve preloaded files
      if (preloadFiles[urlPath]) {
        const filePath = preloadFiles[urlPath];
        if (fs.existsSync(filePath)) {
          res.writeHead(200, {
            "Content-Type": "application/octet-stream",
            "Cross-Origin-Opener-Policy": "same-origin",
            "Cross-Origin-Embedder-Policy": "require-corp",
          });
          fs.createReadStream(filePath).pipe(res);
          return;
        }
        res.writeHead(404);
        res.end("Preload file not found");
        return;
      }

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

      let filePath = path.resolve(buildDir, "." + urlPath);
      if (!filePath.startsWith(buildDir + path.sep)) {
        res.writeHead(403);
        res.end("Forbidden");
        return;
      }

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

      if (!fs.statSync(filePath).isFile()) {
        res.writeHead(404);
        res.end("Not found");
        return;
      }

      // Install pre-init compiler selection before optional automatic startup.
      if (filePath.endsWith(".html") && autoStartScript) {
        let html = fs.readFileSync(filePath, "utf-8");
        // The shell has declared startEmulator before these scripts execute.
        html = html.replace("</body>", autoStartScript + "\n</body>");
        res.writeHead(200, {
          "Content-Type": "text/html",
          "Cross-Origin-Opener-Policy": "same-origin",
          "Cross-Origin-Embedder-Policy": "require-corp",
        });
        res.end(html);
        return;
      }

      res.writeHead(200, {
        "Content-Type": getMime(filePath),
        "Cross-Origin-Opener-Policy": "same-origin",
        "Cross-Origin-Embedder-Policy": "require-corp",
      });
      fs.createReadStream(filePath).pipe(res);
    };
    const server = options.tls
      ? https.createServer(options.tls, handler)
      : http.createServer(handler);
    server.once("error", reject);

    server.listen(port, options.host ?? "127.0.0.1", () => {
      const addr = server.address();
      const resolvedPort = typeof addr === "object" && addr ? addr.port : 0;
      resolve({ server, port: resolvedPort });
    });
  });
}
