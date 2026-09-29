# CPU time varies along with elapsed time

A separate four-run diagnostic followed the rejected combination-removal trial.
It snapshots only descendants of the spawned benchmark before releasing its
existing phase-one gate and after receiving the phase-two measurement. Nothing
is sampled inside the measured window. No affinity, priority or global kernel
setting is changed. Archived binaries are used; source edits made during the
runs do not change those binaries. No owned build, test or other browser runs
concurrently.

| Order/build | Elapsed seconds | Busiest worker CPU seconds |
| --- | ---: | ---: |
| Served 1 | 18.9624 | 17.8524 |
| Wide 1 | 13.4890 | 12.5675 |
| Wide 2 | 15.6722 | 14.7045 |
| Served 2 | 15.8931 | 14.9833 |

The busiest surviving thread is named DedicatedWorker in every run. Independent
user+system CPU ticks broadly agree with its schedstat runtime. The snapshot
interval extends about 0.3 seconds beyond the emulator's measured interval;
these are approximate brackets, not perfectly synchronized CPU accounting.
The thread name alone does not prove that every recorded cycle is guest CPU
execution. New/exited threads are omitted from thread deltas; process counters
are also preserved.

For both unchanged binaries, CPU execution time varies along with wall time.
The slow runs therefore cannot be attributed solely to time waiting to be
scheduled. This does not identify a cause: CPU frequency, execution/cache costs,
browser optimization state and interference remain hypotheses. Context-switch
counts are evidence of switches, not a duration or cause attribution. Kernel
sched_schedstats was disabled, so runnable-wait deltas are unavailable, not zero.
Do not normalize wall timings by these counters, discard slower controls, or
claim that this small diagnostic establishes a compiler gain. Continue serial
controls and retain all results.

Every run covers guest seconds 78–96: 3,975,618,624 instructions and 676
presentations, shared audio enabled, hardware GPU, capture/detail/sampling off.
The fixture and gates are unchanged. Raw snapshots, measurements and binary
hashes are indexed in SCHEDULER_DIAGNOSTIC_EVIDENCE.json.

Reproduce serially with scheduler_probe.py ASSETS ARCHIVED_BUILD NEW_OUTPUT.
The default repository is derived from the script path; --repo overrides it.
This is a Linux diagnostic and requires the existing profile.ts toolchain.

Counter definitions: [Linux proc documentation](https://docs.kernel.org/filesystems/proc.html)
for user/system CPU ticks and context switches, and
[Linux scheduler statistics](https://docs.kernel.org/scheduler/sched-stats.html)
for per-task runtime and runnable-wait fields.
