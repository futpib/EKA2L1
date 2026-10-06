# Frequency-controlled browser comparisons

`controlled_comparison.py` compares frozen browser builds on identical guest
work. It uses ordinary Chromium tiering and the hardware GPU, with sampling,
tracing, verification and guest diagnostics disabled. The JSON plan fixes the
builds, settings, game windows, run orders and measurement-validity rules before
execution. `ABBA` followed by `BAAB` supplies four observations per variant.

Use `fixed_frequency.py` to request equal minimum and maximum CPU frequencies
for the duration of the command. On the benchmark host, CPU 7 and SMT sibling
15 form one physical core. The guest worker runs on CPU 7; other benchmark
threads and unrelated user-space cgroups use the remaining CPUs. The root
helper and comparison controller also inherit this support-only CPU mask. The
Python measurement process pins itself before spawning its clock-monitor thread;
every clock sample checks that monitor's affinity too. Reserving a core from
other cgroups alone would still leave these measuring processes eligible for it.
The root
helper must run in the top-level `ekabench.slice`, so it can reserve the core
without restricting its own child. It restores the saved frequency policies
and CPU placement after completion, failure or a handled interruption.

```sh
sudo -n systemd-run --scope --quiet \
  --unit=ekabench-comparison --slice=ekabench.slice -- \
  python3 src/tests/benchmark/fixed_frequency.py \
  --khz 3600000 --isolate-cpus 7,15 --state /ABS/NEW_OUTPUT/host.json -- \
  python3 src/tests/benchmark/controlled_comparison.py \
  /ABS/PLAN.json /ABS/NEW_OUTPUT/runs
```

The state-file parent must exist and the state file must be new. The child runs
as the invoking sudo user. A failed campaign can resume from the same plan and
run directory using a new host-state filename. Completed valid observations
are retained; a changed plan hash is rejected. `--only NAME ...` selects specific
experiments from a plan. Do not run comparison campaigns concurrently.

## Measurement validity

The helper's frequency request is insufficient evidence by itself.
`scheduler_probe.py` pins the busiest warmup `DedicatedWorker` at the start
gate, then checks that it is also the dominant measured worker. Where the
browser harness reports its worker PID/TID, that identity must agree too.
Kernel scheduler runtime provides CPU seconds. User-space hardware counters
provide retired instructions, cycles and reference cycles, without sampling.
Counter identity and full enabled/running time are required.

Actual frequency is derived from cycles/reference-cycles and a separately
calibrated invariant reference frequency. On this host the reference is
2304 MHz, measured against `CLOCK_MONOTONIC_RAW`; do not substitute the sysfs
base-frequency label or copy this value to another processor without checking.
Clock samples every 250 ms record affinity, frequency-policy readback, thermal
counters, other cgroups' effective CPUs and per-CPU activity.

The campaign plan specifies tolerances independently of measured speed. Invalid
clock, isolation, throttling or counter observations remain in `observations.json`
with their errors. The runner retries that same variant, up to three attempts;
three failures in one invocation stop the campaign. An explicit resume preserves
those failures and permits three further attempts. Interrupted directories that
never produced an observation are renamed and retained before retrying. A slow
result that passes these rules stays
in the performance comparison. Do not change thresholds after seeing a result
or correct elapsed seconds by multiplying them by an estimated clock ratio.

Each run records build hashes, harness hashes, exact configuration, guest work,
presentation-journal hash when capture is enabled, wall seconds, CPU seconds
and hardware counters. Historical selector experiments can require explicit
report readbacks through `expected_control` and `expected_candidate`. Archived
harnesses are selected with `harness`; the emulator builds remain frozen.
Before each trial the driver waits until known compiler/build processes finish,
recording the wait. It does not stop or suspend unrelated work. This is a start
condition, not a retrospective rule for discarding slow measurements. New work
can still start during a trial, so measured clock and isolation checks remain
necessary. The explicitly requested audio mode is preserved and read back.

## Limits and restoration

Core isolation does not isolate the shared last-level cache, memory bandwidth,
GPU or package power. Kernel threads and interrupts can still run on the reserved
core. Reversed orders and independent browsers remain necessary; a stable
frequency does not imply identical timing or prove a small performance effect.
CPU time includes kernel work; hardware counters exclude it. Snapshot and counter
boundaries approximately bracket the guest window. Warmup stays outside it.

Linux 6.12 can reject clearing an explicit CPU mask on a populated cgroup with
`ENOSPC`. In that case the helper restores the original effective CPUs as an
explicit mask and records `restored_as_explicit_mask`; this is not byte-for-byte
restoration of an originally empty, inherited field. No persistent service
configuration is written. Restoration evidence is in the host-state JSON.

The helper cannot run cleanup after `SIGKILL` or a machine crash. Preserve its
state file and use ordinary termination when interrupting an owned campaign.
The benchmark scripts do not change the emulator's runtime defaults or deploy
browser assets.

Focused validation:

```sh
python3 src/tests/benchmark/test_controlled_comparison.py
```

These tests reject clock drift, affinity changes, changed policy, throttling,
multiplexed/missing counters, stale identity and sibling contention. Real browser
runs are still required to establish that the controls work on the target host.
