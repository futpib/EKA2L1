import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export const buildDir = path.resolve(__dirname, "../../../build-wasm/src/emu/wasm");

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
  return `
<script>
async function autoStart() {
  // Wait for WASM runtime to be fully initialized
  while (typeof Module === 'undefined' || !Module.calledRun) {
    await new Promise(r => setTimeout(r, 100));
  }
  // Extra delay to ensure all runtime initialization is complete
  await new Promise(r => setTimeout(r, 200));
  Module.setStatus('Auto-loading...');

  async function fetchToFile(url, emPath) {
    const res = await fetch(url);
    if (!res.ok) return null;
    const buf = new Uint8Array(await res.arrayBuffer());
    var parts = emPath.split('/');
    var dir = '';
    for (var i = 0; i < parts.length - 1; i++) {
      dir += '/' + parts[i];
      try { FS.mkdir(dir); } catch(e) {}
    }
    FS.writeFile(emPath, buf);
    return emPath;
  }

  try {
    await Module.ccall('eka2l1_init', 'number', ['string'], ['/data'], {async: true});

    var romRes = await fetch('/preload/rom');
    if (romRes.ok) {
      Module.setStatus('Uploading ROM...');
      var romPath = await fetchToFile('/preload/rom', '/tmp/SYM.ROM');
      var rpkgPath = null;
      var rpkgRes = await fetch('/preload/rpkg');
      if (rpkgRes.ok) {
        Module.setStatus('Uploading RPKG...');
        rpkgPath = await fetchToFile('/preload/rpkg', '/tmp/SYM.RPKG');
      }
      Module.setStatus('Installing device...');
      var ret = await Module.ccall('eka2l1_install_device', 'number', ['string', 'string'], [romPath, rpkgPath || ''], {async: true});
      if (ret !== 0) throw new Error('Device install failed: ' + ret);
    }

    var sisRes = await fetch('/preload/sis');
    if (sisRes.ok) {
      Module.setStatus('Uploading SIS...');
      var sisPath = await fetchToFile('/preload/sis', '/tmp/app.sis');
      Module.setStatus('Installing SIS...');
      var ret = await Module.ccall('eka2l1_install_sis', 'number', ['string'], [sisPath], {async: true});
      if (ret !== 0) Module.printErr('SIS install returned ' + ret);
    }

    Module.setStatus('Starting ${appName}...');
    var runRet = await Module.ccall('eka2l1_run', 'number', ['string'], ['${appName}'], {async: true});
    if (runRet !== 0) throw new Error('eka2l1_run failed: ' + runRet);
    Module.setStatus('Running: ${appName}');
  } catch(e) {
    Module.setStatus('Error: ' + e.message);
    Module.printErr(e.toString());
  }
}
autoStart();
</script>`;
}

export function startServer(
  port = 0,
  preloadFiles: Record<string, string> = {},
  appName?: string,
): Promise<{ server: http.Server; port: number }> {
  return new Promise((resolve) => {
    const autoStartScript = makeAutoStartScript(appName);

    const server = http.createServer((req, res) => {
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

      // Inject auto-start script into HTML (before the emscripten module script
      // so onRuntimeInitialized is set before the module loads)
      if (filePath.endsWith(".html") && autoStartScript) {
        let html = fs.readFileSync(filePath, "utf-8");
        // Insert before the closing </body> — the Module.onRuntimeInitialized
        // hook is set early in the page's first <script> block via our injection
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
    });

    server.listen(port, "127.0.0.1", () => {
      const addr = server.address();
      const resolvedPort = typeof addr === "object" && addr ? addr.port : 0;
      resolve({ server, port: resolvedPort });
    });
  });
}
