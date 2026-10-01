# Long-run headroom and memory investigation

Measured 2026-09-27 on the same i7-10875H / NVIDIA Quadro T1000 Max-Q / Chrome
150.0.7871.186 desktop. **The heavy gameplay stretch remains barely realtime,
but progressive slowdown from an accumulating emulator leak was not reproduced
in this route.** This does not rule out small leaks, longer sessions, other game
states, thermal throttling or memory pressure on another device.

## Method and scope

Two deterministic, unpaced benchmark runs advance to 600 guest seconds. Rendering
is enabled; frame readback/PNG capture, detailed performance counters and audio
export retention are disabled. Guest audio consumption/callbacks remain intact.
The monitor records atomic guest counters, allocator statistics, instantiated
compiled-function counts, page heap, browser-process proportional resident memory
(PSS), and a scene image each guest minute. Host observations never drive the
guest clock. Trials run serially, after builds/tests finish, on a shared host.

The initial unchanged input route reaches high scores around the fourth minute.
Its later menu timings are excluded from gameplay claims. The second run uses
`snakes-long.input`, re-entering gameplay at four- and eight-minute offsets
without resetting the emulator. Screenshots confirm gameplay in all three rounds.
The second run enables CPU sampling at 541.531325 guest seconds; this interval includes profiler overhead and another heavy gameplay stretch.
The sharp browser-memory rise begins when profiling starts. Do not interpret its final-minute spike
as a leak or compare those timings as an unprofiled control.

## Headroom and progression

| Active gameplay interval | First run | Repeated-round run |
| --- | ---: | ---: |
| 60–120 guest seconds | 1.118x realtime | 1.125x |
| Heavy 78–96 seconds | **1.042x** | **1.037x** |
| 120–180 seconds | 1.132x | 1.126x |
| 300–360 seconds, second round | menu, excluded | **1.126x** |
| 360–420 seconds, second round | menu, excluded | **1.133x** |
| 510–535 seconds, third round | menu, excluded | **1.166x** |

These are interpolated host durations at fixed guest-time endpoints from samples
about two host seconds apart. First-round 40–60s and second-round 280–300s measure
1.146x and 1.151x respectively. Later gameplay has not become slower. The heavy
78–96s window demands about **220.5 million guest instructions per guest second**,
versus about **156 million** during 22–60s. That measured workload increase is
consistent with a scene-dependent slowdown; it does not prove the cause of the
user's experience on an unspecified client. Some short windows still dip below
1x, consistent with the earlier live runs' temporary lag.

## Memory

- First run: allocated emulator memory **545.55 → 546.30 MiB**; linear-memory
  capacity stays **569.0625 MiB**. Compiled functions grow **12,943 → 15,948**,
  predominantly during warmup; only 36 more appear between 300 and 600 seconds.
- Repeated-round run before CPU sampling (539.09s): allocator **545.55 →
  546.50 MiB**, same fixed linear capacity; compiled functions **12,943 → 16,613**.
  End-of-run allocator is about 546.56 MiB despite the profiler's browser growth.
- Browser PSS is much larger: about **1.85–1.93 GiB** in the repeated run before
  sampling. It grows modestly; this is not proof that every browser/GPU allocation
  is leak-free. Late process-level samples (358–432s) show renderer PSS about
  **1548.8 → 1549.1 MiB**, while GPU-process PSS stays near 198 MiB.
- The pool reports **19 running / 45 unused workers** throughout sampled gameplay.
  The configured pool size is 64. This identifies unnecessary-looking fixed
  capacity, not a demonstrated speed gain from shrinking it.

No large unbounded allocator growth or corresponding progressive slowdown is
observed over this test. Small residual browser growth remains unclassified.
The footprint itself could matter on a lower-memory client even without a leak.
Probe work totals 4.59s and 6.98s respectively, included in elapsed time along
with screenshots. This is a diagnostic comparison, not zero-overhead timing.

## Next options

1. **Optimize the measured generated-code hotspots.** Late sampling attributes
   56.69% of the guest-worker span to generated functions and their callees.
   Top game entry points include `0x7006370c` and `0x70013edc`. Inspect these
   emitted routines and try budget-correct loop fusion / memory-span reuse or
   targeted arithmetic simplification. Keep exact instruction exits and replay
   equality; the previous broad budget-batching experiment did not earn a gain.
2. **Reduce repeated region lookup/validation work.** `validated_code_cache::find`
   accounts for 16.90% self samples and `lookup_compiled` 5.71%. Improve guarded
   successor reuse and region boundaries so useful work per validated entry
   increases. Do not remove exact byte checks without complete code-write tracking.
   These costs alone are not a guaranteed wall-time speedup.
3. **Reduce fixed memory pressure.** Trial a 32-worker pool (19 observed running,
   with startup/late peaks checked) and audit temporary uploaded ROM/RPKG/SIS
   buffers after installation. Test startup, thread creation, gameplay and shutdown
   before keeping it. This is especially relevant to weaker clients; desktop CPU
   speed gains are unmeasured.

The graphics worker is about 95.8% sampled waiting in the late window. More GPU
work or generic opcode coverage is not the best-supported first target. CPU
sample shares are not additive across workers or predictive speed multipliers.
Aim for at least 1.25x unpaced throughput on heavy gameplay windows, then repeat
sustained live input tests on the actual client device. No new performance
optimization was made in this investigation.

## Validation and reproduction

The monitoring build matches native across the first **80 images, guest
timestamps and instruction records** through 25 guest seconds. Discarded PCM is
intentionally not compared in this monitor mode. All 131 WASM tests, three native
test targets and seven frontend checks pass. Earlier 1,600-frame correctness evidence remains in REALTIME_PLAYABILITY.md;
this investigation does not claim a new 600-second pixel comparison.

From `src/tests/wasm`:

```sh
EKA2L1_BENCHMARK_AOT=5 EKA2L1_GPU=hardware EKA2L1_PROFILE_DETAIL=0 \
EKA2L1_LONG_MONITOR=1 node profile.ts ASSETS OUTPUT_A 2 0 600000000
EKA2L1_BENCHMARK_AOT=5 EKA2L1_GPU=hardware EKA2L1_PROFILE_DETAIL=0 \
EKA2L1_LONG_MONITOR=1 EKA2L1_PROFILE_INPUT=../benchmark/snakes-long.input \
EKA2L1_MONITOR_CPU_START_US=540000000 \
node profile.ts ASSETS OUTPUT_B 2 0 600000000
python3 ../benchmark/summarize_long_run.py OUTPUT_A OUTPUT_B
python3 ../benchmark/summarize_profile.py OUTPUT_B
```

Raw records, source/build hashes, fixed-window measurements and CPU samples:
`LONG_RUN_EVIDENCE.json`. Large artifacts remain under
`/home/claude/.scratch/eka-benchmark/long600-2` and `long600-rounds`.
