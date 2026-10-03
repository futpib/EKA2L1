import fs from 'node:fs';
import type {CDPSession} from 'puppeteer';

type ProcessTime = {id: number; type: string; cpuTime: number};
type ThreadTime = {pid: number; tid: number; name: string; birth_ticks: string; runtime_ns: string};
export type CpuSnapshot = {
  host_ms: number;
  collection_ms: number;
  processes: ProcessTime[];
  threads: ThreadTime[];
  thread_errors: string[];
};

// comm can contain spaces and closing parentheses. Field 22 identifies a task
// lifetime so a recycled TID cannot silently become a negative/huge CPU delta.
export function taskBirth(stat: string) {
  const end = stat.lastIndexOf(') ');
  const fields = stat.slice(end + 2).trim().split(/\s+/);
  if (end < 0 || fields.length < 20 || !/^\d+$/.test(fields[19]))
    throw new Error('Invalid Linux task stat');
  return fields[19];
}

export async function sampleCpuTime(client: Pick<CDPSession, 'send'>): Promise<CpuSnapshot> {
  const started = performance.now();
  const {processInfo} = await client.send('SystemInfo.getProcessInfo');
  const threads: ThreadTime[] = [], errors: string[] = [];
  if (process.platform === 'linux') {
    for (const proc of processInfo.filter(p => p.type === 'renderer')) {
      const base = `/proc/${proc.id}/task`;
      try {
        for (const id of fs.readdirSync(base)) {
          try {
            const task = `${base}/${id}`;
            const stat = fs.readFileSync(`${task}/stat`, 'utf8');
            const runtime = fs.readFileSync(`${task}/schedstat`, 'utf8').trim().split(/\s+/)[0];
            if (!/^\d+$/.test(runtime)) throw new Error('Invalid scheduler runtime');
            threads.push({pid: proc.id, tid: Number(id), name: fs.readFileSync(`${task}/comm`, 'utf8').trim(),
              birth_ticks: taskBirth(stat), runtime_ns: runtime});
          } catch (error) { errors.push(`${proc.id}/${id}: ${String(error)}`); }
        }
      } catch (error) { errors.push(`${proc.id}: ${String(error)}`); }
    }
  } else errors.push('Per-thread scheduler counters require a local Linux browser');
  return {host_ms: started, collection_ms: performance.now() - started,
    processes: processInfo, threads, thread_errors: errors};
}

export function cpuTimeDelta(before: CpuSnapshot, after: CpuSnapshot) {
  const processes = after.processes.map(p => {
    const old = before.processes.find(x => x.id === p.id && x.type === p.type);
    return {pid: p.id, type: p.type, cpu_seconds: old ? p.cpuTime - old.cpuTime : null,
      status: old ? (p.cpuTime >= old.cpuTime ? 'matched' : 'counter_reset') : 'new_process'};
  });
  const missingProcesses = before.processes.filter(p => !after.processes.some(x => x.id === p.id && x.type === p.type));
  const renderers = processes.filter(p => p.type === 'renderer');
  const rendererComplete = renderers.length > 0 && renderers.every(p => p.status === 'matched')
    && !missingProcesses.some(p => p.type === 'renderer');
  const threads = after.threads.map(t => {
    const old = before.threads.find(x => x.pid === t.pid && x.tid === t.tid && x.birth_ticks === t.birth_ticks);
    const ns = old ? BigInt(t.runtime_ns) - BigInt(old.runtime_ns) : null;
    return {pid: t.pid, tid: t.tid, name: t.name, cpu_seconds: ns !== null && ns >= 0n ? Number(ns) / 1e9 : null,
      status: ns === null ? 'new_or_reused_thread' : ns < 0n ? 'counter_reset' : 'matched'};
  }).sort((a, b) => (b.cpu_seconds ?? -1) - (a.cpu_seconds ?? -1));
  const missingThreads = before.threads.filter(t => !after.threads.some(x => x.pid === t.pid && x.tid === t.tid && x.birth_ticks === t.birth_ticks));
  return {
    source: 'Chrome SystemInfo.getProcessInfo; Linux /proc/PID/task/TID/schedstat runtime',
    scope: 'Host samples bracket resume and observed completion, including polling margins. Renderer totals include CPU emulation, rendering, audio and compiler threads; GPU device time is excluded.',
    renderer_cpu_seconds: rendererComplete ? renderers.reduce((sum, p) => sum + p.cpu_seconds!, 0) : null,
    renderer_complete: rendererComplete,
    host_interval_seconds: (after.host_ms - before.host_ms) / 1000,
    collection_ms: {before: before.collection_ms, after: after.collection_ms},
    processes, missing_processes: missingProcesses.map(p => ({pid: p.id, type: p.type})),
    threads, missing_threads: missingThreads.map(t => ({pid: t.pid, tid: t.tid, name: t.name})),
    thread_errors: [...before.thread_errors, ...after.thread_errors],
    busiest_renderer_thread: threads.find(t => t.cpu_seconds !== null) ?? null,
    thread_attribution: 'Busiest thread is an observation, not verified guest-worker identity.',
  };
}
