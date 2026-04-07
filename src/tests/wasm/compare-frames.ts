import fs from "node:fs";
import path from "node:path";
import { PNG } from "pngjs";

const rootDir = path.resolve(import.meta.dirname, "../../..");
const qtDir = process.argv[2] || path.join(rootDir, "build-wasm/frames-qt");
const wasmDir = process.argv[3] || path.join(rootDir, "build-wasm/frames-wasm");

// Max allowed difference per channel (0-255) to account for rounding/scaling
const CHANNEL_TOLERANCE = 8;
// Max fraction of pixels allowed to differ
const PIXEL_DIFF_THRESHOLD = 0.02;

function loadPng(filePath: string): PNG {
  const data = fs.readFileSync(filePath);
  return PNG.sync.read(data);
}

function comparePngs(
  qtPath: string,
  wasmPath: string,
): { match: boolean; details: string; diffPath?: string } {
  const qt = loadPng(qtPath);
  const wasm = loadPng(wasmPath);

  if (qt.width !== wasm.width || qt.height !== wasm.height) {
    return {
      match: false,
      details: `size mismatch: qt=${qt.width}x${qt.height} wasm=${wasm.width}x${wasm.height}`,
    };
  }

  const totalPixels = qt.width * qt.height;
  let diffPixels = 0;
  let maxDiff = 0;
  let maxDiffR = 0, maxDiffG = 0, maxDiffB = 0;
  let maxDiffX = 0, maxDiffY = 0;

  // Create diff image: green = match, red = difference (amplified)
  const diff = new PNG({ width: qt.width, height: qt.height });

  for (let y = 0; y < qt.height; y++) {
    for (let x = 0; x < qt.width; x++) {
      const idx = (y * qt.width + x) * 4;
      const dr = Math.abs(qt.data[idx] - wasm.data[idx]);
      const dg = Math.abs(qt.data[idx + 1] - wasm.data[idx + 1]);
      const db = Math.abs(qt.data[idx + 2] - wasm.data[idx + 2]);
      const pixelDiff = Math.max(dr, dg, db);

      if (pixelDiff > CHANNEL_TOLERANCE) {
        diffPixels++;
        if (pixelDiff > maxDiff) {
          maxDiff = pixelDiff;
          maxDiffR = dr;
          maxDiffG = dg;
          maxDiffB = db;
          maxDiffX = x;
          maxDiffY = y;
        }
        // Red channel shows amplified difference
        diff.data[idx] = Math.min(255, pixelDiff * 4);
        diff.data[idx + 1] = 0;
        diff.data[idx + 2] = 0;
        diff.data[idx + 3] = 255;
      } else {
        // Dim green for matching pixels
        diff.data[idx] = 0;
        diff.data[idx + 1] = Math.max(qt.data[idx], qt.data[idx + 1], qt.data[idx + 2]) / 4;
        diff.data[idx + 2] = 0;
        diff.data[idx + 3] = 255;
      }
    }
  }

  const diffFraction = diffPixels / totalPixels;
  const match = diffFraction <= PIXEL_DIFF_THRESHOLD;

  let details = `${diffPixels}/${totalPixels} pixels differ (${(diffFraction * 100).toFixed(2)}%)`;
  if (diffPixels > 0) {
    details += `, max channel diff=${maxDiff} (R=${maxDiffR} G=${maxDiffG} B=${maxDiffB}) at (${maxDiffX},${maxDiffY})`;
    const qtIdx = (maxDiffY * qt.width + maxDiffX) * 4;
    const wasmIdx = (maxDiffY * wasm.width + maxDiffX) * 4;
    details += ` qt=(${qt.data[qtIdx]},${qt.data[qtIdx+1]},${qt.data[qtIdx+2]}) wasm=(${wasm.data[wasmIdx]},${wasm.data[wasmIdx+1]},${wasm.data[wasmIdx+2]})`;
  }

  // Save diff image if there are differences
  let diffPath: string | undefined;
  if (diffPixels > 0) {
    diffPath = wasmPath.replace(/\.png$/, "-diff.png");
    fs.writeFileSync(diffPath, PNG.sync.write(diff));
  }

  return { match, details, diffPath };
}

// Clean old diff images
for (const f of fs.readdirSync(wasmDir)) {
  if (f.match(/-diff\.png$/)) {
    fs.unlinkSync(path.join(wasmDir, f));
  }
}

// Find matching frame files
const qtFiles = fs.readdirSync(qtDir).filter((f) => f.match(/^frame-\d+\.png$/)).sort();
const wasmFiles = fs.readdirSync(wasmDir).filter((f) => f.match(/^frame-\d+\.png$/)).sort();

if (qtFiles.length === 0) {
  console.error(`No Qt frames found in ${qtDir}`);
  process.exit(1);
}
if (wasmFiles.length === 0) {
  console.error(`No WASM frames found in ${wasmDir}`);
  process.exit(1);
}

// Match by filename
const allNames = new Set([...qtFiles, ...wasmFiles]);
let allPass = true;
let compared = 0;

for (const name of [...allNames].sort()) {
  const qtPath = path.join(qtDir, name);
  const wasmPath = path.join(wasmDir, name);

  if (!fs.existsSync(qtPath)) {
    console.log(`SKIP ${name}: no Qt reference`);
    continue;
  }
  if (!fs.existsSync(wasmPath)) {
    console.log(`FAIL ${name}: missing in WASM output`);
    allPass = false;
    continue;
  }

  const result = comparePngs(qtPath, wasmPath);
  compared++;

  if (result.match) {
    console.log(`PASS ${name}: ${result.details}`);
  } else {
    console.log(`FAIL ${name}: ${result.details}`);
    if (result.diffPath) {
      console.log(`     diff: ${result.diffPath}`);
    }
    allPass = false;
  }
}

console.log(`\n${compared} frames compared, ${allPass ? "ALL PASS" : "SOME FAILED"}`);
process.exit(allPass ? 0 : 1);
