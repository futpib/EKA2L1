# Expanded browser CPU kernels, 2026-09-30

All five ARM projects now run four checked ARM32 loop bodies without a guest OS.
One million iterations per observation. Ten measured observations per cell; all
initial runs, warmups and checks retained in BROWSER_CORES_EXPANDED_EVIDENCE.json.

| Direct adapter | Arithmetic ms | Indexed 1 KiB RAM ms | Conditions ms | Dependent 64 KiB reads ms |
|---|---:|---:|---:|---:|
| skyemu7 | 128.72 | 131.96 | 145.67 | 132.74 |
| cloudpilot | 103.39 | 122.63 | 130.25 | 142.00 |
| rpcemu | 43.98 | 46.56 | 57.93 | 44.58 |
| skyemu9 | 138.87 | 142.30 | 156.17 | 142.50 |
| EKA generated regions only | 4.73 | 6.67 | 7.85 | 11.42 |

Separate semihosting method, same ARM loop bodies on bare virt/cortex-a15.
All 128 completed outputs match the reference (two fresh browsers per core).
Repetitions 0–2 retained as initial/warmup; 3–7 used below.

| Prebuilt CPU path | Arithmetic ms | Indexed RAM ms | Conditions ms | Dependent reads ms |
|---|---:|---:|---:|---:|
| QEMU Wasm demo JIT | 11.31 | 17.30 | 16.13 | 14.16 |
| Pebble repository TCI | 152.39 | 185.23 | 257.02 | 187.33 |

The dependent workload follows a full-period pointer chain over 16,384 words,
performing two dependent loads per iteration. It covers multiple guest pages
but is still a tiny hot instruction loop with no interrupts, faults, devices,
code mutation, or OS. This is not a representative application suite.

EKA excludes ARM translation and outer code validation/dispatch. RPCEmu uses
prepopulated direct mappings; SkyEmu includes pipeline phases; Cloudpilot retains
decoded instruction caching and RAM bookkeeping. QEMU and TCI include their
machine CPU/memory path, but host marker delivery is a different timing method.
The measured Pebble repository TCI is not its newer hosted JIT.

The fourth workload passes without changing the broad earlier conclusion: a
browser guest-code JIT can strongly outperform these interpreters on hot loops.
No end-to-end Snakes speedup or universally fastest core follows from this.
The existing live emulator is unchanged.
