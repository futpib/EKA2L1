# Full EKA CPU browser control, 2026-09-30

Yes within this small warmed-kernel suite: EKA's normal CPU runner records
lower times on all four workloads in both batches than the refreshed QEMU JIT
control. This closes the earlier omission of EKA's outer CPU execution path.
It does not establish the fastest complete emulator or predict Snakes performance.

One million iterations, median milliseconds (lower is faster):

| Serial batch | Arithmetic | Indexed 1 KiB RAM | Conditions | Dependent 64 KiB reads |
| --- | ---: | ---: | ---: | ---: |
| EKA full runner D | 5.73 | 7.09 | 7.98 | 12.08 |
| QEMU JIT E | 11.12 | 15.80 | 16.43 | 14.20 |
| EKA full runner F | 5.73 | 7.77 | 7.98 | 12.15 |
| QEMU JIT G | 10.61 | 17.87 | 16.25 | 14.51 |

Each EKA batch contains two page instances, five measured repetitions per
workload per instance. Each QEMU batch uses a fresh browser with five measured
repetitions per workload. First execution, warmups, small-count checks, setup,
initialization and all measured observations are retained in the evidence.
EKA/QEMU/EKA/QEMU execute serially, with no concurrent owned build or timing job.
Both batches of EKA also remain below every previously measured interpreter and
the separate non-ARM equivalent-algorithm rows, but those are not interchangeable
CPU implementations or timing methods.

EKA uses `dyncom_core::run`, demand RAM compilation, interpreter fallback,
validated cache lookup, exact byte comparisons, generated dispatch and bounded
instruction budgets. Runtime choices match the delivered policy: emitter 7,
folded TLB indexing and grouped exact scanner. Code versions, lifecycle and
write protection are disabled in a separate clean build. It uses flat bounded
RAM callbacks and prepopulated TLB pages, without Symbian or peripheral work.
Measured runs reuse compiled code. Initial arithmetic execution including demand
translation/module compilation took 21.48–32.34 ms in the first batch; this is
distinct from warmed throughput.

All 212 timed/check executions across four EKA page instances match registers
R0–R14 and the complete data hash. Each page also passes an untimed census and
host-code mutation/restoration check. The census records 8,009,998 compiled
instructions, 801 dispatches and zero interpreted instructions for arithmetic.
The mutation test clears only the interpreter's decoded cache, leaving the
compiled cache and mapping generation intact; changed results and restoration
verify that stale compiled code is rejected. Eight guest functions are compiled.
All 64 QEMU completed outputs match the algorithm reference.

The QEMU method includes semihost marker delivery; EKA uses an exported-function
timer. The small instruction loops amortize outer dispatch/validation and do not
stress many short regions, faults, interrupts or code changes during timing.
Different memory paths and timing methods limit interpretation, particularly for
the smaller dependent-read lead. No universal emulator victory is claimed.

Three incomplete harness attempts are retained separately: missing malloc export,
a diagnostic using a counter disabled in this configuration, and a mutation test
that omitted interpreter decoded-cache coherence. They provide no accepted timing
samples. The final adapter fixes these issues without changing the CPU implementation.
No emulator deployment or push accompanies this benchmark.
