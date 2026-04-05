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

export function startServer(port = 0): Promise<{ server: http.Server; port: number }> {
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

    server.listen(port, "127.0.0.1", () => {
      const addr = server.address();
      const resolvedPort = typeof addr === "object" && addr ? addr.port : 0;
      resolve({ server, port: resolvedPort });
    });
  });
}
