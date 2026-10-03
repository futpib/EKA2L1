import fs from 'node:fs';
import type {CDPSession} from 'puppeteer';
import type {Protocol} from 'devtools-protocol';

// Browser-wide tracing includes compiler/GPU threads and workers created after
// recording starts. No Debugger domain or precise coverage: both change code.
export const traceCategories = [
  'toplevel', 'devtools.timeline', 'disabled-by-default-devtools.timeline',
  'disabled-by-default-devtools.timeline.frame', 'blink.user_timing',
  'v8', 'v8.wasm', 'v8.execute', 'disabled-by-default-v8.gc',
  'disabled-by-default-v8.cpu_profiler',
];

type TraceResult = {file: string; bytes: number; data_loss: boolean; categories: string[]};

export class ChromeTrace {
  private active = false;
  private client: CDPSession;
  private filename: string;
  private categories = traceCategories;
  private stopping: Promise<TraceResult> | undefined;
  constructor(client: CDPSession, filename: string) {
    this.client = client;
    this.filename = filename;
  }

  async start(cpuSamples = true) {
    // Sampling every isolate throughout startup emits enormous duplicate code
    // logs. Whole-run traces use CDP profiles for the measured window instead.
    this.categories = cpuSamples ? traceCategories : traceCategories.filter(c => !c.endsWith('v8.cpu_profiler'));
    await this.client.send('Tracing.start', {
      transferMode: 'ReturnAsStream', streamFormat: 'json',
      traceConfig: {recordMode: 'recordUntilFull', traceBufferSizeInKb: 131072,
        includedCategories: this.categories},
    });
    this.active = true;
  }

  async stop() {
    if (this.stopping) return this.stopping;
    if (!this.active) return null;
    this.active = false;
    this.stopping = this.finish();
    return this.stopping;
  }

  private async finish(): Promise<TraceResult> {
    const complete = new Promise<Protocol.Tracing.TracingCompleteEvent>(resolve =>
      this.client.once('Tracing.tracingComplete', resolve));
    await this.client.send('Tracing.end');
    const {stream, dataLossOccurred} = await complete;
    if (!stream) throw new Error('Chrome did not return a trace stream');
    const fd = fs.openSync(this.filename, 'wx');
    let bytes = 0;
    try {
      for (;;) {
        const chunk = await this.client.send('IO.read', {handle: stream, size: 1024 * 1024});
        const data = Buffer.from(chunk.data, chunk.base64Encoded ? 'base64' : 'utf8');
        fs.writeFileSync(fd, data);
        bytes += data.length;
        if (chunk.eof) break;
      }
    } finally {
      fs.closeSync(fd);
      await this.client.send('IO.close', {handle: stream});
    }
    return {file: this.filename, bytes, data_loss: dataLossOccurred, categories: this.categories};
  }
}

// Names already emitted into each generated module encode its entry address.
// Decode them after capture, retaining the raw identity and module URL; RAM
// versions at the same guest PC must not be merged. A PC is not an instruction
// offset or a DLL name, and an entry can include inlined guest callees.
export function guestEntry(name: string) {
  const rom = /^f_(\d+)$/.exec(name);
  const ram = /^r_(\d+)_pc_(\d+)$/.exec(name);
  if (!rom && !ram) return null;
  const address = Number(rom ? rom[1] : ram![2]);
  if (!Number.isSafeInteger(address) || address < 0 || address > 0xffffffff) return null;
  return {pc: `0x${(address - (address % 2)).toString(16).padStart(8, '0')}`,
    entry_key: address, version: ram ? Number(ram[1]) : null};
}

export function summarizeProfile(name: string, profile: Protocol.Profiler.Profile) {
  const nodes = new Map(profile.nodes.map(n => [n.id, n]));
  const weights = new Map<number, number>();
  for (let i = 0; i < (profile.samples?.length ?? 0); ++i) {
    const id = profile.samples![i];
    weights.set(id, (weights.get(id) ?? 0) + (profile.timeDeltas?.[i] ?? 0));
  }
  const samplesUs = [...weights.values()].reduce((sum, value) => sum + value, 0);
  let generatedUs = 0;
  const frames = [...weights].map(([id, selfUs]) => {
    const frame = nodes.get(id)!.callFrame;
    const guest = frame.url.startsWith('wasm://') ? guestEntry(frame.functionName) : null;
    if (guest) generatedUs += selfUs;
    return {node_id: id, name: frame.functionName, url: frame.url, guest,
      self_us: selfUs, self_percent: samplesUs ? selfUs / samplesUs * 100 : 0};
  }).sort((a, b) => b.self_us - a.self_us);
  return {name, file: `${name}.cpuprofile`, samples: profile.samples?.length ?? 0,
    span_us: profile.endTime - profile.startTime, sampled_us: samplesUs,
    generated_self_us: generatedUs, frames};
}

export function labelGuestProfile(profile: Protocol.Profiler.Profile) {
  const labelled = structuredClone(profile);
  for (const node of labelled.nodes) {
    const guest = node.callFrame.url.startsWith('wasm://') ? guestEntry(node.callFrame.functionName) : null;
    if (guest) node.callFrame.functionName = `guest ${guest.pc} [${node.callFrame.functionName}]`;
  }
  return labelled;
}
