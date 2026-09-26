# RAM compilation and bounded compiled execution

Runtime revision: 4c46b0ba1dc435d2ad7c1fe82a8a17dfabff232e. This follows the ROM-only results in AOT_RESULTS.md. All work is local; nothing was pushed.

## Results

Both final browser runs (one unchecked, one with verifier stride 1,024) match the native reference across all **1,000 distinct gameplay images**, guest timestamps, presentation ordinals, instruction counts, PCM and audio-event records. They end at **68.040042 guest seconds** and **10,342,580,529 instructions**, covering **47.013860 guest seconds** of captured gameplay. The two earlier pre-prefix-refinement replays also passed, but the final result is attributed specifically to revision 4c46b0ba.

Every timing fixture and the separate sampled profile match the native reference's first 85 gameplay PNGs byte-for-byte and have identical frame records. Each timed window advances exactly 4 guest seconds, 655,867,149 instructions and 170 presentations.

| Trial | Host seconds for 4 guest seconds | Compiled instruction coverage |
| --- | ---: | ---: |
| interpreter-1 | 93.122 | 0.00% |
| hot-rom | 61.524 | 18.84% |
| hot-ram | 46.456 | 69.97% |
| chained-1 | 45.450 | 69.97% |
| chained-2 | 42.316 | 69.97% |
| interpreter-2 | 57.374 | 0.00% |
| confirmation-interpreter | 55.627 | 0.00% |
| confirmation-chained | 39.334 | 69.97% |

The first interpreter control showed substantial shared-host variation: an unrelated Playwright/SwiftShader process was observed during it, and the ending control fell from 93.122 to 57.374 seconds. Do not use the first control alone to claim a multiplier. A fresh serial confirmation pair, after that browser process exited, measured **55.627 seconds interpreter versus 39.334 seconds chained**, or **1.41x throughput** (29.3% less elapsed time) in that pair. This is a shared-host observation, not an isolated hardware guarantee.

Across three chained trials the window takes **39.334–45.450 host seconds**, about **0.088–0.102x realtime**. Realtime is **not achieved**: this measured workload still needs roughly **9.8–11.4x** more throughput, before allowing headroom. No physical-GPU or interactive Qt-JIT comparison was performed.

RAM compilation raises compiled coverage from 18.84% to **69.97%**. Decoded instructions drop from 90,055,999 to **26,137,201** (71.0% fewer). With RAM compilation, the unchained path makes 117,732,931 runner entries; the chained path makes **16,860,275** while executing 117,675,732 blocks, about **6.98 blocks per runner call**. The single-stage trials suggest a smaller wall-time benefit from chaining/locals than from RAM coverage, but changing host load limits attribution. Larger source windows do not guarantee long executed blocks: branches, stores and unsupported forms still limit them.

The final sampled emulation worker attributes **26.35%** of its sampled span to translation, **14.62%** to RAM-cache lookup, **14.11%** to the interpreter loop, **5.67%** to WASM-to-JS transitions, **4.04%** to the mapping resolver and **2.53%** to byte comparison. Waits occupy 7.54%. These are self-sample shares from one worker, not additive cross-thread CPU utilization or measured optimization opportunities. The earlier whole-window byte validation profile showed 16.65% in memcmp, which motivated comparing only the emitted prefix; those profiles ran under different load conditions.

The next compiler bottlenecks are lookup/validation and frequent short compiled-function transitions, alongside remaining interpreter translation. This initial profiling reported host functions and execution counters. The missing guest module/opcode attribution is now completed in [GUEST_PROFILE_RESULTS.md](GUEST_PROFILE_RESULTS.md). Skipping RAM validation would violate the current correctness model.

Machine-readable reports, hashes, exact comparisons and test log hashes: [REALTIME_AOT_EVIDENCE.json](REALTIME_AOT_EVIDENCE.json). Host: Intel Core i7-10875H, 8 cores/16 threads; Chromium 150 with SwiftShader, Emscripten Release -O3.

## Implemented

1. Register-history snapshots and per-module accounting are opt-in (`EKA2L1_AOT_DIAGNOSTICS=1`). Correctness counters and guest instruction accounting remain enabled where needed.
2. Mode 3 compiles hot executable RAM/game pages. A compiled version is keyed by address space and ARM/Thumb PC. Every entry resolves the current executable mapping and checks its backing pointer and exact instruction bytes; explicit invalidation also handles IMB and unmapping. The byte comparison detects alias and host-pointer writes that bypass guest store notifications. Replaced versions cannot receive a late compilation result. Negative entries are bound to the rejected instruction bytes. The cache caps lifetime versions at 16,384; this can limit later coverage during long sessions.
3. Mode 4 adds register/flag WASM locals, 512-byte source windows, and a runner that follows up to 64 compiled successors within the exact remaining instruction budget. Memory callbacks flush cached state and reload it afterward, including registers first used textually after that callback. Each successor checks code validity, ARM/Thumb state and interrupt/stop conditions. Registers are local within a generated block, not across function returns; successor linking is a bounded C++ dispatch loop, not fused cross-block code or direct patched edges.

RAM blocks stay within one executable page and stop after their first store. This avoids executing later generated instructions after the block modifies code. Validation covers only the emitted instruction prefix, not the unused remainder of the translation window. These guards assume serialized guest execution; concurrent external code mutation during an executing block is outside this implementation's scope.

## Correctness scope

The WASM suite passes 123 cases, including 93,072 exact instruction-budget/register/flag/memory comparisons over ARM and Thumb instructions with local caching and stop-after-store both enabled/disabled. Additional tests cover callback register/flag mutation, mapping/ASID/mode separation, byte changes at the end of a compiled prefix, unchanged unused suffixes, remapping, unmapping, stale attachment, exact chain budgets, zero progress, mode changes, interrupts and the chain-length cap.

Native CPU tests pass. The broader native suite has 81 passing cases and seven previously observed failures in allocator, number-parsing and app-registration tests (441/448 assertions); it is not a green package suite. No new clean-base run establishes when these failures began.

The original every-block RAM and chained replay checks were stopped incomplete because their overhead was excessive. They are not passed checks. The final long checked replay uses `EKA2L1_AOT_VERIFY=1024`, comparing every 1,024th compiled block to a separate interpreter state and memory overlay. This selection uses an execution counter, not wall time. Full framebuffer/guest-record comparison remains exact for every captured image.

## Measurement method

All six timing fixtures use the same final runtime and replay inputs, identical full captures, no CPU sampling, no verifier, and no dispatch diagnostics. Guests warm concurrently and pause at 21 guest seconds. After full replay comparisons finish, each 21–25-second guest window is released serially. The final CPU-sampled profile is a separate run after these timings; its elapsed time is not a timing control. The confirmation pair warms two fresh fixtures together, then measures interpreter followed by chained execution serially after that profile has ended. Measurements use headless Chromium with SwiftShader on this host, not a physical GPU or interactive Qt JIT.

Audio-quality work is deferred. PCM equality is retained only as a deterministic regression check. These results do not demonstrate interactive input latency or real-device clock accuracy.

## Reproduce

```sh
cd src/tests/wasm
EKA2L1_BENCHMARK_AOT=4 node benchmark.ts /absolute/assets /absolute/new-output 1000
EKA2L1_BENCHMARK_AOT=4 EKA2L1_AOT_VERIFY=1024 node benchmark.ts /absolute/assets /absolute/new-checked-output 1000
cd ../../..
python3 src/tests/benchmark/compare.py /absolute/native-reference /absolute/new-output
python3 src/tests/benchmark/validate_gameplay.py /absolute/new-output
python3 src/tests/benchmark/profile_batch.py --assets /absolute/assets --output /absolute/new-stage-comparison --compare-stages
```

For a serial confirmation pair, invoke `profile.ts` with capture mode `0` and sampling `0` once with `EKA2L1_BENCHMARK_AOT=0`, then with `EKA2L1_BENCHMARK_AOT=4`, using fresh output directories. Warmup is recorded separately from the measured window.

Mode 0 is interpreter, 2 is hot ROM, 3 adds guarded RAM, and 4 adds locals/chains. Mode 4 remains opt-in. Use `--measure-gate /new/path` to pause all warmed fixtures until correctness or build jobs finish; create that file only once the machine is ready for serial measurement.
