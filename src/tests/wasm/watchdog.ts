export function watchdogInterval(): number {
  const text = process.env.EKA2L1_WATCHDOG_US ?? '0';
  if (!/^(0|[1-9][0-9]*)$/.test(text) || Number(text) > 1000000)
    throw Error('EKA2L1_WATCHDOG_US must be 0 (disabled) or 1..1000000');
  return Number(text);
}

// This function also runs in the browser through Puppeteer's evaluate().
export async function configureWatchdog(intervalUs: number): Promise<void> {
  const g = globalThis as any;
  if (!intervalUs) return;
  const m = g.Module;
  if (m._eka2l1_watchdog_configure(1) !== 0 || m._eka2l1_watchdog_report() !== 1)
    throw Error('Watchdog configuration failed');
  const control = new SharedArrayBuffer(8);
  const worker = new Worker('/watchdog.js');
  await new Promise<void>((resolve, reject) => {
    worker.onmessage = ({data}) => data === 'ready' ? resolve() : reject(Error('Unexpected watchdog reply'));
    worker.onerror = reject;
    worker.postMessage({memory: g.HEAPU8.buffer, address: m._eka2l1_watchdog_address(), intervalUs, control});
  });
  g.ekaWatchdog = {worker, intervalUs, control: new Int32Array(control)};
}
