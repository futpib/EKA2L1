export function watchdogInterval(): number {
  const text = process.env.EKA2L1_WATCHDOG_US ?? '2000';
  if (!/^[1-9][0-9]*$/.test(text) || Number(text) > 1000000)
    throw Error('EKA2L1_WATCHDOG_US must be 1..1000000; watchdog execution is always enabled');
  return Number(text);
}

// This function also runs in the browser through Puppeteer's evaluate().
export async function configureWatchdog(intervalUs: number): Promise<void> {
  const g = globalThis as any;
  if (!Number.isSafeInteger(intervalUs) || intervalUs < 1 || intervalUs > 1000000)
    throw Error('Invalid watchdog interval');
  if (g.ekaWatchdog) return;
  const m = g.Module;
  const control = new SharedArrayBuffer(8);
  const worker = new Worker('/watchdog.js');
  await new Promise<void>((resolve, reject) => {
    worker.onmessage = ({data}) => data === 'ready' ? resolve() : reject(Error('Unexpected watchdog reply'));
    worker.onerror = reject;
    worker.postMessage({memory: g.HEAPU8.buffer, address: m._eka2l1_watchdog_address(), intervalUs, control});
  });
  g.ekaWatchdog = {worker, intervalUs, control: new Int32Array(control)};
}
