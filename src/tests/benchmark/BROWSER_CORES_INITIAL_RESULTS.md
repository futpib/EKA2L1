# Initial browser CPU kernels, 2026-09-30

Partial comparison; work continues. No emulator deployment or general fastest-core claim.

One million fixed guest-loop iterations per observation. Median milliseconds over ten retained measured observations per core/workload (two page instances, forward/reverse order). All small-count and measured outputs pass the independent reference.

| Adapter | Arithmetic | Indexed RAM | Conditional integer |
|---|---:|---:|---:|
| skyemu7 | 128.99 | 131.77 | 146.67 |
| skyemu9 | 139.27 | 142.18 | 156.65 |
| rpcemu | 43.91 | 46.60 | 58.13 |
| cloudpilot | 103.08 | 130.97 | 129.62 |
| eka (generated regions only) | 4.67 | 6.70 | 7.91 |

These figures describe these adapters and tiny, hot, predictable kernels. RPCEmu uses prepopulated direct mappings; Cloudpilot retains its decoded instruction cache and RAM tracking; SkyEmu includes pipeline phases. EKA excludes outer code validation/dispatch and ARM translation, so its row is a generated-code throughput measurement, not an end-to-end CPU replacement result. No guest OS, graphics, audio, interrupts, faults or code mutation are exercised.

Both QEMU binaries complete the same ARM32 loop bodies on a bare virt/cortex-a15 machine. All 96 completed outputs across two fresh-browser runs each match the reference. Failed setup launches are retained in scratch logs, not timing samples. Neither run boots Symbian or firmware.

These use a different measurement method: host receipt of semihosting start/end markers, including delivery overhead. The guest memory pointer differs from the direct adapters. Repetitions 0-2 are retained as initial/warmup, and medians below use repetitions 3-7 from both runs. The order is JIT/TCI, then TCI/JIT.

| Prebuilt full-system CPU path | Arithmetic ms | Indexed RAM ms | Conditional integer ms |
|---|---:|---:|---:|
| QEMU Wasm demo JIT | 11.48 | 16.60 | 16.22 |
| Pebble repository TCI binary | 149.19 | 184.16 | 261.15 |

The QEMU JIT advantage over TCI is substantial on these three tiny loops. This is not a universal ranking: guest memory systems, core fidelity and instrumentation differ; QEMU uses a 32-bit Cortex-A15 on virt, not Pebble hardware. The current Pebble hosted page now also advertises a separate newer JIT build; that artifact is not the repository TCI binary measured here. The QEMU demo artifact has recorded hashes but no inferred correspondence to the current source checkout.

Non-ARM v86/Flycast require separately compiled equivalent algorithms. See browser_cores/README.md for scope and reproduction; all observations are in BROWSER_CORES_INITIAL_EVIDENCE.json.

## Longer direct-adapter confirmation

Four million iterations, forward and reverse order, all checks pass. Median milliseconds over ten measured observations per cell (all warmups and first runs retained in BROWSER_CORES_LONGER_EVIDENCE.json):

| Adapter | Arithmetic | Indexed RAM | Conditions |
|---|---:|---:|---:|
| skyemu7 | 517.08 | 526.79 | 581.57 |
| cloudpilot | 416.00 | 516.94 | 521.84 |
| rpcemu | 176.91 | 186.70 | 238.10 |
| skyemu9 | 556.59 | 568.72 | 625.24 |
| eka | 18.80 | 26.83 | 31.50 |

Ordering is unchanged. EKA remains generated regions only. This confirms run-length stability for these kernels, not workload representativeness.
