# Browser benchmark CPU time

The harness now records actual Chrome renderer-process CPU seconds and Linux
per-thread scheduler runtimes alongside the existing guest-window wall clock.
No emulator rebuild or per-instruction instrumentation is needed. See
[usage and limitations](CHROME_PROFILING.md#timing-and-experiment-decisions) and
[raw evidence](CPU_TIME_RESULTS.json).

CPU time excludes waiting/descheduling. It does not eliminate CPU-frequency,
cache, compilation, or active-contention variation. The measurements below do
not establish that CPU time alone makes this shared host's results stable.

## Validation

Six focused CPU-counter and profiler tests passed, including counter resets,
process/thread churn, a task name containing parentheses, and precise subtraction
of large cumulative counters. The serial driver also passed Python compilation.
A real Chromium worker test distinguished a 1.2-second wait (0.12 renderer CPU
seconds) from a 0.6-second busy loop (0.61 renderer CPU seconds; 0.600 worker CPU
seconds). That is the discriminating property an elapsed-time proxy would fail.

A separate Snakes diagnostic trace matched generated ARM-code profile events to
OS thread 3427002 in renderer 3426957. This was the same thread selected as busiest
by scheduler-runtime deltas. The identity check applies to that diagnostic run;
the throughput runs below keep profiling disabled and label the observed busiest
thread without asserting its identity.

## Fixed-work follow-up

One serial ABBA panel per game used the same frozen control and candidate
binaries as the [span-page-reuse experiment](SPAN_PAGE_REUSE_RESULTS.md). Only the
current host harness added CPU snapshots. Custom diagnostics, tracing and CPU
sampling were disabled. All runs retained identical guest instruction totals and
presentation counts for their game. No owned build, profiler or other benchmark
overlapped these runs. Outside host activity was uncontrolled.

All values below are seconds. Renderer CPU is summed over renderer processes;
it includes other workers and compiler work. The busiest-thread column is a
single observed renderer thread's CPU runtime, excluding off-CPU time.

| # | Game | Build | Wall | Renderer CPU | Busiest-thread CPU |
|---|---|---|---:|---:|---:|
| 1 | Snakes | control | 2.402750 | 2.660000 | 2.073263 |
| 2 | Snakes | candidate | 2.184100 | 2.400000 | 1.890657 |
| 3 | Snakes | candidate | 2.263650 | 2.480000 | 1.947679 |
| 4 | Snakes | control | 2.156840 | 2.390000 | 1.839149 |
| 5 | Sky Force | control | 9.916210 | 9.810000 | 9.187239 |
| 6 | Sky Force | candidate | 9.947500 | 9.880000 | 9.241998 |
| 7 | Sky Force | candidate | 10.074100 | 10.000000 | 9.358654 |
| 8 | Sky Force | control | 10.199000 | 10.070000 | 9.465039 |

Snakes: mean candidate throughput change is +2.5% by wall time, +3.5% by renderer CPU, and +1.9% by busiest-thread CPU. The two busiest-thread pair changes are +9.7%, -5.6%.

Sky Force: mean candidate throughput change is +0.5% by wall time, +0.0% by renderer CPU, and +0.3% by busiest-thread CPU. The two busiest-thread pair changes are -0.6%, +1.1%.

The earlier Snakes slowdown did not reproduce consistently in this later panel,
and CPU time still varies between control runs. These are separate observation
sets; the old wall results are preserved rather than overwritten. There is still
no repeatable general gain justifying the span-cache optimization's adoption.
The measurement capability is adopted; the optimization remains out of production.

Snapshots had no missing renderer threads or read errors in this panel. Their
collection overhead is recorded for each run and occurs at the pause/completion
boundaries, outside guest execution. A null CPU total or a missing/reused task in
a future report must not be interpreted as zero work.
