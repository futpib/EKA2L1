import assert from 'node:assert/strict';
import test from 'node:test';
import {guestEntry, labelGuestProfile, summarizeProfile} from './chrome-profiler.ts';

test('guest identities retain RAM versions and reject non-address names', () => {
  assert.equal(guestEntry('f_2149177368')?.pc, '0x8019d818');
  assert.equal(guestEntry('f_4097')?.pc, '0x00001000');
  assert.equal(guestEntry('r_42_pc_4096')?.version, 42);
  assert.equal(guestEntry('f_4096_budget_short')?.pc, '0x00001000');
  assert.equal(guestEntry('f_8192_memory_fallback')?.pc, '0x00002000');
  for (const name of ['f_4294967296', 'f_-1', 'f_12x', 'f_4096_unknown', 'wasm-function[9]', 'rom_dispatch'])
    assert.equal(guestEntry(name), null);
});

test('time weighting preserves distinct stacks and modules; labels do not alter samples', () => {
  const frame = (id: number, name: string, url: string) => ({id, callFrame: {
    functionName: name, url, scriptId: String(id), lineNumber: 0, columnNumber: 10,
  }});
  const profile = {startTime: 100, endTime: 700, nodes: [
    frame(1, 'f_4096', 'wasm://wasm/one'), frame(2, 'f_4096', 'wasm://wasm/two'),
    frame(3, 'f_4096', 'http://localhost/eka2l1.wasm'),
    frame(4, '__pthread_cond_timedwait', 'http://localhost/eka2l1.wasm'),
  ], samples: [1, 2, 1, 3, 4], timeDeltas: [10, 40, 50, 200, 300]};
  const original = structuredClone(profile);
  const summary = summarizeProfile('worker-13', profile);
  assert.equal(summary.sampled_us, 600);
  assert.equal(summary.generated_self_us, 100);
  assert.deepEqual(summary.frames.map(n => n.self_us), [300, 200, 60, 40]);
  assert.equal(summary.frames.filter(n => n.guest).length, 2);
  const labelled = labelGuestProfile(profile);
  assert.deepEqual(profile, original);
  assert.deepEqual(labelled.samples, original.samples);
  assert.deepEqual(labelled.timeDeltas, original.timeDeltas);
  assert.equal(labelled.nodes[0].callFrame.functionName, 'guest 0x00001000 [f_4096]');
  assert.equal(labelled.nodes[2].callFrame.functionName, 'f_4096');
  const privateProfile = {startTime: 0, endTime: 30, nodes: [
    frame(1, 'f_4096_budget_short', 'wasm://wasm/one'),
    frame(2, 'f_4096_memory_fallback', 'wasm://wasm/two'),
  ], samples: [1, 2], timeDeltas: [10, 20]};
  assert.equal(summarizeProfile('worker-13', privateProfile).generated_self_us, 30);
});
