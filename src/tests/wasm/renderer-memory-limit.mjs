import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn, execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const [game, limitText, output, serverUrl] = process.argv.slice(2);
const limitMiB = Number(limitText);
const tabClientId = process.env.EKA2L1_TAB_RENDERER_CLIENT_ID;
if (tabClientId && !/^[1-9]\d*$/.test(tabClientId)) throw Error('Invalid tab renderer client ID');
if (!game || !Number.isSafeInteger(limitMiB) || limitMiB < 64 ||
    !output || !path.isAbsolute(output) || !serverUrl) {
  throw Error('Usage: node renderer-memory-limit.mjs GAME LIMIT_MIB NEW_OUTPUT SERVER_URL');
}

const here = path.dirname(fileURLToPath(import.meta.url));
const prefix = path.join(here, 'renderer-cgroup-prefix.sh');
const routeOutput = path.join(output, 'route');
const unit = `eka2l1-tab-ram-${process.pid}-${crypto.randomBytes(4).toString('hex')}.service`;
const limitBytes = limitMiB * 1024 * 1024;
if (!Number.isSafeInteger(limitBytes)) throw Error('Memory limit is too large');
const timeoutMs = Number(process.env.EKA2L1_TAB_RAM_TIMEOUT_MS || 600000);
if (!Number.isSafeInteger(timeoutMs) || timeoutMs < 1000) throw Error('Invalid timeout');
const smapsThresholds = (process.env.EKA2L1_CGROUP_SMAPS_AT_MIB || '').split(',')
  .filter(Boolean).map(Number).sort((a, b) => a - b);
if (smapsThresholds.some(value => !Number.isSafeInteger(value) || value < 1))
  throw Error('Invalid smaps threshold');
fs.mkdirSync(output);

const systemctl = (...args) => execFileSync('systemctl', ['--user', ...args], {encoding: 'utf8'}).trim();
const readNumber = file => Number(fs.readFileSync(file, 'utf8').trim());
const readEvents = file => Object.fromEntries(fs.readFileSync(file, 'utf8').trim().split('\n')
  .map(line => { const [key, value] = line.split(' '); return [key, Number(value)]; }));
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const terminateGroup = child => {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  try { process.kill(-child.pid, 'SIGTERM'); } catch {}
  setTimeout(() => {
    if (child.exitCode !== null || child.signalCode !== null) return;
    try { process.kill(-child.pid, 'SIGKILL'); } catch {}
  }, 5000).unref();
};

let child, monitor, timeout, started = false, interrupted = false;
const samples = [];
const result = {game, limitMiB, unit, serverUrl, routeOutput,
  scope: tabClientId ? 'game-tab renderer' : 'all browser renderers'};
try {
  execFileSync('systemd-run', ['--user', '--collect', `--unit=${unit}`,
    `--property=MemoryMax=${limitBytes}`, '--property=MemorySwapMax=0',
    '--property=OOMPolicy=continue', '/usr/bin/sleep', 'infinity'], {encoding: 'utf8'});
  started = true;
  let group;
  for (let attempt = 0; attempt < 50; attempt++) {
    group = systemctl('show', unit, '--property=ControlGroup', '--value');
    if (group) break;
    await delay(100);
  }
  if (!group?.startsWith('/user.slice/') || group.includes('..')) throw Error('No safe renderer cgroup');
  const cgroup = '/sys/fs/cgroup' + group;
  if (readNumber(path.join(cgroup, 'memory.max')) !== limitBytes ||
      readNumber(path.join(cgroup, 'memory.swap.max')) !== 0) {
    throw Error('Renderer memory limit was not applied');
  }
  result.cgroup = group;
  result.baselineBytes = readNumber(path.join(cgroup, 'memory.current'));
  const sample = () => {
    const row = {ms: Date.now(), currentBytes: readNumber(path.join(cgroup, 'memory.current')),
      peakBytes: readNumber(path.join(cgroup, 'memory.peak')),
      events: readEvents(path.join(cgroup, 'memory.events'))};
    samples.push(row);
    while (smapsThresholds.length && row.currentBytes >= smapsThresholds[0] * 1048576) {
      const threshold = smapsThresholds.shift();
      const pids = fs.existsSync(path.join(output, 'renderer-pids.txt')) ?
        fs.readFileSync(path.join(output, 'renderer-pids.txt'), 'utf8').trim().split('\n') : [];
      const capture = {thresholdMiB: threshold, ...row, renderers: []};
      for (const pid of pids) {
        if (!/^\d+$/.test(pid)) continue;
        try {
          const rollup = fs.readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8');
          fs.writeFileSync(path.join(output, `threshold-${threshold}-${pid}.smaps`),
            fs.readFileSync(`/proc/${pid}/smaps`));
          capture.renderers.push({pid: Number(pid),
            pssKiB: Number(rollup.match(/^Pss:\s+(\d+)/m)?.[1] || 0)});
        } catch {}
      }
      fs.writeFileSync(path.join(output, `threshold-${threshold}.json`),
        JSON.stringify(capture, null, 2) + '\n');
    }
    if (row.events.oom > 0 || row.events.oom_kill > 0) terminateGroup(child);
    return row;
  };
  const log = fs.openSync(path.join(output, 'route.log'), 'w');
  child = spawn(process.execPath, ['arena-growth-benchmark.mjs', game, routeOutput, serverUrl], {
    cwd: here, detached: true, stdio: ['ignore', log, log],
    env: {...process.env, EKA2L1_BENCHMARK_PERSISTENT_DISK: '1',
      EKA2L1_RENDERER_CGROUP_PREFIX: prefix, EKA2L1_RENDERER_CGROUP: cgroup,
      EKA2L1_RENDERER_PID_LOG: path.join(output, 'renderer-pids.txt')}
  });
  result.runnerPid = child.pid;
  fs.closeSync(log);
  sample();
  monitor = setInterval(() => { try { sample(); } catch (error) { result.monitorError = String(error); } }, 200);
  timeout = setTimeout(() => { result.timedOut = true; terminateGroup(child); }, timeoutMs);
  const stop = () => { interrupted = true; terminateGroup(child); };
  process.once('SIGINT', stop);
  process.once('SIGTERM', stop);
  const exit = await new Promise(resolve => {
    child.once('error', error => resolve({error: String(error)}));
    child.once('close', (code, signal) => resolve({code, signal}));
  });
  process.off('SIGINT', stop);
  process.off('SIGTERM', stop);
  clearInterval(monitor); monitor = undefined;
  clearTimeout(timeout); timeout = undefined;
  result.exit = exit;
  result.final = sample();
  result.peakMiB = result.final.peakBytes / 1048576;
  result.samples = samples.length;
  result.rendererProcesses = fs.existsSync(path.join(output, 'renderer-pids.txt')) ?
    fs.readFileSync(path.join(output, 'renderer-pids.txt'), 'utf8').trim().split('\n').length : 0;
  if (tabClientId && fs.existsSync(path.join(routeOutput, 'tab-renderer.json')))
    result.tabRenderer = JSON.parse(fs.readFileSync(path.join(routeOutput, 'tab-renderer.json'), 'utf8'));
  result.cappedRendererProcesses = tabClientId ? Number(!!result.tabRenderer) : result.rendererProcesses;
  const routeReports = fs.existsSync(routeOutput) ? fs.readdirSync(routeOutput)
    .filter(name => name.endsWith('.json'))
    .flatMap(name => {
      try {
        const report = JSON.parse(fs.readFileSync(path.join(routeOutput, name), 'utf8'));
        return report.game === game && report.label && Number.isFinite(report.frames) ? [report] : [];
      } catch { return []; }
    }) : [];
  const routeReport = routeReports.at(-1);
  result.route = routeReport && {label: routeReport.label, frames: routeReport.frames, fps: routeReport.fps,
    observedBrowserPeakPssKiB: routeReport.observedBrowserPeakPssKiB};
  result.status = result.final.events.oom > 0 || result.final.events.oom_kill > 0 ? 'oom' :
    result.timedOut ? 'timeout' : interrupted ? 'interrupted' :
    exit.code === 0 && routeReport && result.rendererProcesses > 0 && (!tabClientId || result.tabRenderer)
      ? 'pass' : 'route-error';
} catch (error) {
  result.status = 'setup-error';
  result.error = String(error);
} finally {
  clearInterval(monitor);
  clearTimeout(timeout);
  if (child?.pid) {
    try { process.kill(-child.pid, 'SIGTERM'); } catch {}
  }
  if (started) {
    try { systemctl('stop', unit); } catch (error) { result.cleanupError = String(error); }
  }
  fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify(result, null, 2) + '\n');
  fs.writeFileSync(path.join(output, 'memory-samples.json'), JSON.stringify(samples) + '\n');
}

console.log(JSON.stringify({status: result.status, limitMiB, peakMiB: result.peakMiB,
  oom: result.final?.events.oom, oomKill: result.final?.events.oom_kill,
  cappedRendererProcesses: result.cappedRendererProcesses, output}));
process.exitCode = result.status === 'pass' ? 0 : result.status === 'oom' ? 2 : 1;
