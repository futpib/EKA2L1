import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';
const args = process.argv.slice(2);
if (args.length !== 4 || args[0] !== '--build' || args[2] !== '--output') {
  throw Error('Usage: node resource-semantic-memory-proof.mjs --build DIR --output REPORT');
}
const build = path.resolve(args[1]);
const output = path.resolve(args[3]);
const digest = value => crypto.createHash('sha256').update(value).digest('hex');
const report = {schema: 1, pass: false, scope: 'Actual multiple/flexible MMU backing publication and semantic-view authority; no guest execution or hardware-permission claim.', models: [], errors: []};
try {
  const receiptBytes = await fs.readFile(path.join(build, 'build.json'));
  const receipt = JSON.parse(receiptBytes);
  if (!receipt.pass) throw Error('Memory control build failed');
  report.build_sha256 = digest(receiptBytes);
  for (const [file, expected] of Object.entries(receipt.outputs)) {
    if (digest(await fs.readFile(path.join(build, file))) !== expected) throw Error(`Build output changed: ${file}`);
  }
  const create = (await import(pathToFileURL(path.join(build, 'memory.mjs')).href)).default;
  for (const [index, model] of ['multiple', 'flexible'].entries()) {
    const row = {model, controls: null, log: []};
    report.models.push(row);
    const module = await create({print: line => row.log.push(line), printErr: line => row.log.push(line)});
    row.controls = module._memory_controls(index);
    if (row.controls !== 64) throw Error(`Expected 64 actual controls for ${model}`);
  }
  report.pass = true;
} catch (error) {
  report.errors.push(error.stack ?? String(error));
}
report.driver_sha256 = digest(await fs.readFile(new URL(import.meta.url)));
await fs.mkdir(path.dirname(output), {recursive: true});
await fs.writeFile(output, JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report));
if (!report.pass) process.exitCode = 1;
