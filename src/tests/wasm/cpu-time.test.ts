import assert from 'node:assert/strict';
import test from 'node:test';
import {cpuTimeDelta, taskBirth, type CpuSnapshot} from './cpu-time.ts';

test('task identity handles parentheses in Linux comm', () => {
  const fields = ['S', ...Array(18).fill('0'), '987654', '0'];
  assert.equal(taskBirth(`123 (worker ) (test)) ${fields.join(' ')}`), '987654');
  assert.throws(() => taskBirth('truncated'));
});

const snapshot = (): CpuSnapshot => ({host_ms: 1000, collection_ms: 2,
  processes: [{id: 10, type: 'renderer', cpuTime: 40}, {id: 20, type: 'GPU', cpuTime: 8}],
  threads: [{pid: 10, tid: 11, name: 'worker', birth_ticks: '100', runtime_ns: '90071992547409930'}],
  thread_errors: []});

test('CPU deltas exclude elapsed waiting and preserve nanosecond differences', () => {
  const before = snapshot(), after = snapshot();
  after.host_ms += 5000;
  after.processes[0].cpuTime += 0.3;
  after.processes[1].cpuTime += 0.1;
  after.threads[0].runtime_ns = String(BigInt(before.threads[0].runtime_ns) + 234567891n);
  const result = cpuTimeDelta(before, after);
  assert.ok(Math.abs(result.renderer_cpu_seconds! - 0.3) < 1e-10);
  assert.equal(result.host_interval_seconds, 5);
  assert.equal(result.busiest_renderer_thread?.cpu_seconds, 0.234567891);
});

test('process churn and recycled task IDs cannot masquerade as complete CPU totals', () => {
  const before = snapshot(), after = snapshot();
  after.processes[0].id = 30;
  after.threads[0].birth_ticks = '200';
  const result = cpuTimeDelta(before, after);
  assert.equal(result.renderer_complete, false);
  assert.equal(result.renderer_cpu_seconds, null);
  assert.deepEqual(result.missing_processes, [{pid: 10, type: 'renderer'}]);
  assert.equal(result.threads[0].cpu_seconds, null);
  assert.equal(result.busiest_renderer_thread, null);
  assert.equal(result.missing_threads.length, 1);
});

test('counter resets are flagged instead of reporting negative CPU time', () => {
  const before = snapshot(), after = snapshot();
  after.processes[0].cpuTime = 1;
  after.threads[0].runtime_ns = '1';
  const result = cpuTimeDelta(before, after);
  assert.equal(result.renderer_cpu_seconds, null);
  assert.equal(result.threads[0].status, 'counter_reset');
  assert.equal(result.threads[0].cpu_seconds, null);
});
