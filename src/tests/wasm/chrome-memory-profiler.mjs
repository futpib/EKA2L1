import fs from 'node:fs';
import path from 'node:path';

// Diagnostic allocation captures, never a throughput measurement. Native
// Memory samples and V8 HeapProfiler samples describe different allocators.
export class ChromeMemoryProfiler {
  constructor(page, output) {
    this.page = page;
    this.output = output;
    this.targets = [];
    this.pending = [];
    this.onWorker = worker => this.pending.push(this.attach(`worker-${this.targets.length}`, worker.client));
  }

  async attach(name, client) {
    const target = {name, client, errors: [], heap: false, native: false};
    this.targets.push(target);
    try {
      await client.send('HeapProfiler.enable');
      await client.send('HeapProfiler.startSampling', {samplingInterval: 16384, stackDepth: 64});
      target.heap = true;
    } catch (error) { target.errors.push({method: 'HeapProfiler.startSampling', error: String(error)}); }
    // Native sampling is renderer-wide. Starting it on every worker would
    // reset the same sampling interval repeatedly.
    if (name === 'page') try {
      await client.send('Memory.startSampling', {samplingInterval: 16384, suppressRandomness: true});
      target.native = true;
    } catch (error) { target.errors.push({method: 'Memory.startSampling', error: String(error)}); }
  }

  async start() {
    this.page.on('workercreated', this.onWorker);
    await this.attach('page', await this.page.createCDPSession());
    for (const worker of this.page.workers()) this.pending.push(this.attach(`worker-${this.targets.length}`, worker.client));
    await Promise.all(this.pending);
  }

  async stop() {
    this.page.off('workercreated', this.onWorker);
    await Promise.all(this.pending);
    const summaries = [];
    for (const target of this.targets) {
      const result = {target: target.name, errors: target.errors};
      if (target.heap) try {
        const {profile} = await target.client.send('HeapProfiler.stopSampling');
        fs.writeFileSync(path.join(this.output, `${target.name}.heapprofile`), JSON.stringify(profile));
        const nodes = [], visit = node => {nodes.push(node);for (const child of node.children ?? []) visit(child);};
        visit(profile.head);
        result.v8 = {sampled_bytes: nodes.reduce((sum, node) => sum + node.selfSize, 0),
          nodes: nodes.length, wasm_frames: nodes.filter(node => /\.wasm(?:\?|$)|^wasm:\/\//.test(node.callFrame.url)),
          top: nodes.filter(node => node.selfSize).sort((a,b) => b.selfSize-a.selfSize).slice(0,15)
            .map(node => ({bytes: node.selfSize, frame: node.callFrame}))};
      } catch (error) {result.errors.push({method: 'HeapProfiler.stopSampling', error: String(error)});}
      if (target.native) try {
        const {profile} = await target.client.send('Memory.getSamplingProfile');
        fs.writeFileSync(path.join(this.output, `${target.name}.native-memory.json`), JSON.stringify(profile));
        result.native = {samples: profile.samples.length, attributed_bytes: profile.samples.reduce((sum,s) => sum+s.total,0),
          wasm_labelled_stacks: profile.samples.filter(s => s.stack.some(frame => /wasm|eka2l1/i.test(frame))).length,
          top: [...profile.samples].sort((a,b) => b.total-a.total).slice(0,10)};
        await target.client.send('Memory.stopSampling');
      } catch (error) {result.errors.push({method: 'Memory.getSamplingProfile', error: String(error)});}
      summaries.push(result);
    }
    fs.writeFileSync(path.join(this.output, 'chrome-memory-summary.json'), JSON.stringify(summaries,null,2));
    return summaries;
  }
}
