# Shared ARM exit writeback

A shared result block replaces duplicated cached-state writeback at ARM returns.
Three serial real-Snakes batches improve mean throughput by **3.2%, 9.3%, and
2.3%**. The ranges overlap; this is a modest repeated gain on a shared host,
not a precise universal improvement. All observations, including the slow final
candidate, are retained in `SHARED_EXIT_EVIDENCE.json`.

Baseline: `86fd4aaf5`, with the graduated runner, deferred memory, compact guards
and shared audio path. No other optimization is combined with this change.

## Mechanism and correctness

Bounded ARM functions with cached registers now wrap their body in a WASM i32
result block. Each existing return branches to that block's end carrying its
instruction count. One epilogue writes back the cached guest state and returns
that count. The emitter tracks structured-control depth so nested budget, fault,
branch and loop exits reach the same epilogue. Uncached ARM and Thumb retain their
previous return paths.

Helper barriers still publish/reload state at their original positions. PC
publication, every instruction-budget check, memory permissions/alignment/endian
handling, code-write exits, byte/dependency validation and interrupt handling
remain. This does not defer observable state past a callback or discard any
flags. It changes emitted control flow and code duplication, not guest semantics.
There is no game/address whitelist. The browser's resulting lowering contributes
to the measured effect; source-code size alone is not a speed estimate.

This differs from the rejected sparse-flush experiment: that selected fewer
stores separately at each return; this shares the final writeback across returns.

The candidate passes:

- 134 WASM tests, including 453,936 exact bounded state/memory comparisons and
  nested regions, code-write aliases, callbacks and deferred memory exits.
- 672/672 native/WASM fault cases, including exact callback state/order and
  changed memory bytes; 356 cases exercise deferred execution.
- All three native test targets.
- A checked 1,600-image native replay through 102.484363 guest seconds,
  16,263,331,210 guest instructions and 4,919,249 stereo PCM frames. Pixels,
  guest records, PCM and audio events match exactly.

The existing extended replay movement heuristic has a known failure on the
native reference as well: a 408,561-us guest presentation gap and some small
frame changes. Its thresholds were not changed and it is not claimed to pass.
Exact replay equality is a separate gate. No other game or device was tested.

## Serial unprofiled gameplay timing

Each run warms and measures alone, with no owned build/test/profile job running.
Physical NVIDIA GPU, Chromium 153.0.8010.52, scene 78–96 guest seconds, rendering
on, capture/profiling/detailed counters off. Shared DSP/Cubeb audio processing is
explicitly enabled; these benchmarks retain audio exports and do not play through
the host AudioWorklet. Actual playback is tested separately below.

Every run executes **3,975,618,624 guest instructions and 676 presentations**.
The first two batches use baseline/candidate/candidate/baseline. The third uses
candidate/baseline/baseline/candidate to reverse the ordering. Classify the third
batch by its build path, not the harness's before/after labels.

| Batch | Baseline seconds | Candidate seconds | Means, baseline to candidate | Throughput gain |
| --- | --- | --- | --- | --- |
| First | 14.8266 / 14.1241 | 13.8728 / 14.1823 | 14.47535 to 14.02755 | 3.19% |
| Confirmation | 16.1301 / 13.8356 | 13.8743 / 13.5512 | 14.98285 to 13.71275 | 9.26% |
| Reversed order | 14.3797 / 16.5993 | 13.7538 / 16.5419 | 15.48950 to 15.14785 | 2.26% |

All three batch means improve, and five of six adjacent comparison pairs favor
the candidate. Ranges overlap: the candidate does not beat every control, and
one candidate takes 16.54s. Candidate throughput is **1.19–1.31x realtime by
batch means**, not consistently above 1.25x. These results support graduating a
small repeated gain under the user's acceptance of modest improvements; they
do not establish a fixed 9% improvement, a universal speedup, or Qt parity.
Prior stage gains must not be added or multiplied with these measurements.
No separate investigation of unrelated host load was undertaken.

## Live playback

Two separate two-minute physical-GPU runs exercise real upload/Start and
automatic startup, keyboard/touch, narrow layout, blur release and shutdown.

| Route | Guest seconds | Host seconds | Paced ratio | Maximum sampled lag |
| --- | --- | --- | --- | --- |
| Manual Start | 120.531128 | 120.527842 | 1.000027x | 20 ms |
| Automatic Start | 120.529186 | 120.548477 | 0.999840x | 27 ms |

Both have **zero additional audio underruns or dropped samples during the
measured gameplay**. Maximum sampled worklet queue is 174ms / 147ms. These are
paced playback measurements, not spare-throughput estimates or guarantees of
zero short stutters. Startup still records 6/7 underruns and queue recovery
before the measured window. Cold-start audio is not claimed fixed.

## Reproduction

Archives: `~/.scratch/eka-benchmark/epilogue-baseline` and
`~/.scratch/eka-benchmark/epilogue-candidate`. Source the existing process-local
GPU environment `~/.scratch/eka-benchmark/validity-gpu.env`, then run:

```sh
EKA2L1_SHARED_AUDIO=1 python3 src/tests/benchmark/serial_build_comparison.py \
  ASSETS BASELINE_ARCHIVE CANDIDATE_ARCHIVE NEW_OUTPUT
```

Reverse the two archive arguments for the third batch. Build and run the normal
`test_aot_wasm` target and `eka_cpu_fault_wasm --deferred` / native fault
`--extended` comparison. `benchmark.ts` uses `EKA2L1_SHARED_AUDIO=1`,
`EKA2L1_BENCHMARK_AOT=5`, and optionally `EKA2L1_AOT_VERIFY=1024` for the checked
1,600-image replay with `snakes.input` and start 21000000. Live tests use
`EKA2L1_WASM_BUILD_DIR=CANDIDATE_ARCHIVE EKA2L1_LIVE_AUDIO=1` and
`live.ts ASSETS NEW_OUTPUT 120`, optionally `EKA2L1_LIVE_AUTOSTART=1`.

Final delivery checks and hashes are recorded below and in the evidence file.

## Final delivery

Graduated as **`04771f8fe`** on local `wasm-port`. The additional normal
(non-checker) 1,600-image replay also matches native pixels, guest records, PCM
and audio events exactly. All seven frontend checks pass.

The exact tested candidate archive is served at **https://claude-laptop.lan:8188/**.
Its WASM SHA-256 is
`a98b82e1b4d25482d1adc72e7d9bed7819c121872beb7941ed91f86b83977a8b`,
matching the local build, timing/live archive and HTTPS download. The trusted
HTTPS launcher passes secure context/isolation, gesture-enabled sound, measured
nonzero/zero/nonzero mute transitions, keyboard, touch, narrow layout and shutdown.
All 12 canvas samples are visible, with no page/request/HTTP errors and no
underruns or drops after enabling sound following loading. This is a test from
the desktop using its LAN hostname, not a separate physical LAN client.

No push was made. The served baseline has been replaced by this verified
candidate; the archived original remains available for reproduction.
