// The watchdog runs outside WASM and outside the emulator worker's event loop.
// A timed atomic wait avoids chained JavaScript timer clamping. The deadline is
// a yield request, not a bound on a proved terminating guest computation.
self.onmessage = ({data: {memory, address, intervalUs, control}}) => {
  const flag = new Int32Array(memory, address, 1);
  const state = new Int32Array(control);
  postMessage('ready');
  while (!Atomics.load(state, 0)) {
    Atomics.wait(state, 0, 0, intervalUs / 1000);
    if (Atomics.load(state, 0)) break;
    Atomics.store(flag, 0, 1);
    Atomics.add(state, 1, 1);
  }
};
