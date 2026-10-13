# ARM-to-WASM compiler bookkeeping results

Cacheable CPU fields now use a fixed bitmap for written/used membership and a sorted contiguous local-slot table. Slot numbers still follow first-use order, and transfer emission still follows field-offset order. Deferred call relocation uses cumulative barrier lengths instead of scanning every barrier for every call. Small reload/flush encoding buffers are reused per compiler thread; `finish` has no callback or recursive invocation, and both buffers are cleared before each use.

This changes the C++ translation work. Generated-code policy, state transfer pruning, watchdog, memory backend, clock and instruction-accounting behavior remain the existing defaults. No launch option was added.

## Fresh installation comparison

Baseline is `ba711d287`, including WasmFS and the SWI installed-file hash bypass. All runs use fresh Chromium, browser cache and guest installation through the normal paced launcher, without a profiler. Both servers use the same stock firmware, game assets and frontend. Final comparison order is baseline, candidate, candidate, baseline; one exploratory intermediate run precedes the final candidate runs and is excluded from the means. Builds and browser measurements were serialized.

The endpoint is the successful N-Gage Installer thread exit, with the **Installation complete** screen reviewed. It excludes manual game launch and gameplay. Translation counters measure elapsed translation-phase time, including allocation and scheduling; they are not isolated compiler CPU time.

| # | Hooked On run | Fresh installation wall seconds | Translation seconds | Compiled functions |
| ---: | --- | ---: | ---: | ---: |
| 1 | baseline-hooked-a | 53.411 | 9.260 | 98,930 |
| 2 | candidate-hooked-a | 51.606 | 8.362 | 98,907 |
| 3 | final-hooked-a | 51.642 | 8.280 | 98,915 |
| 4 | final-hooked-b | 54.408 | 9.567 | 98,965 |
| 5 | baseline-hooked-b | 58.207 | 10.419 | 98,938 |

Final two-run means: **55.81 -> 53.03 seconds** to installation complete (**4.99% less wall time**), and **9.84 -> 8.92 seconds** in translation (**9.31% less elapsed phase time**). The largest initial precompile batch (20,661 entries) takes 2,463.8 ms in baseline A versus 2,182.8 / 2,209.6 ms in the two final candidates.

Two observations per final variant do not establish statistical significance. Startup ranges overlap, and the later runs are slower on both variants. The result supports a modest compiler/startup improvement, not a guaranteed 5% gain on every run or device. No separate speedup is attributed to buffer reuse or any other individual part of the combined change.

## Correctness and runtime checks

4,000 deterministic comparisons against the original cache implementation preserve function bytes, local counts, source marks and outlined call offsets exactly, with pruning both enabled and disabled. They cover shuffled field use, high state offsets, nested entry scopes, duplicate barrier positions and calls recorded out of order. The saved oracle and original header are in the raw evidence directory. This is fixture equivalence, not a full-game module byte comparison.

All **188 ARM/Thumb WASM correctness tests** pass. The 26 state-liveness fixtures, source-map checks and all three native CTest suites pass. New permanent fixtures cover high architectural fields, nested entry scopes, and relocation across differently sized/adjacent barriers with unordered calls.

Hardware-rendered Chromium checks reached the tutorial-dismissed Costa Rica boat in Hooked On, the level-1 Marathon board in Tetris, and the opening Instant Play race in Asphalt 3. Asphalt was reviewed with position/lap HUDs and sound enabled; initial countdown/launch work remains in its short window. Browser output was muted, so this is not a host speaker-output test. All accepted windows run continuously unpaced, retain zero instruction totals and report no guest/page errors.

| # | Game | Variant | Window seconds | FPS | New compiled functions |
| ---: | --- | --- | ---: | ---: | ---: |
| 1 | hooked-on | baseline | 10.15 | 176.23 | 7 |
| 2 | hooked-on | candidate | 10.15 | 181.34 | 5 |
| 3 | tetris | candidate | 5.06 | 127.69 | 28 |
| 4 | tetris | baseline | 5.05 | 161.43 | 12 |
| 5 | tetris | candidate | 15.15 | 152.06 | 32 |
| 6 | tetris | baseline | 15.17 | 134.76 | 19 |
| 7 | asphalt-3 | candidate | 5.05 | 104.26 | 99 |
| 8 | asphalt-3 | baseline | 5.06 | 103.00 | 95 |

Hooked On measures 176.23 -> 181.34 FPS; Asphalt 3 measures 103.00 -> 104.26 FPS. Tetris first measures 161.43 -> 127.69 FPS over five seconds, then reverses to 134.76 -> 152.06 FPS in fresh fifteen-second runs with three guest seconds of warmup. Both Tetris comparisons are retained. No stable runtime FPS gain or loss is established by these variable captures; the change adds no per-frame work, and compiler bookkeeping equivalence is checked separately. Compilation inside the accepted windows totals about 2-6 ms for Hooked On/Tetris and 22-23 ms for Asphalt, which cannot explain those large FPS fluctuations.

Four rejected Asphalt attempts remain in the raw evidence: one route-evaluation syntax error before inputs, two long menu routes that returned to the attract demo instead of racing, and one baseline launch error (-100002) followed by inputs into the wrong menus. None provides a valid FPS result. Reviewed short inputs and a launch retry reached the real race on both builds. The checked opening race is the same red car/track, but the route is not a frame-exact replay and the baseline launch starts later in guest time.

Adopt the combined compiler change. The evidence supports reduced startup work with no repeatable gameplay throughput loss identified in the three checked games. It does not prove unchanged performance on all ten games or different hardware.

The active `eka2l1-lan.service` serves the frozen final build at `https://claude-laptop.lan:8188/`. Downloaded WASM bytes match the candidate hash below exactly. A fresh browser smoke through the public normal launcher displays the Snakes scene/menu with 151 presentations, zero instruction totals and no guest/page errors. The temporary preview server is removed after verification.

## Artifacts and reproduction

Hardware: Intel(R) Core(TM) i7-10875H CPU @ 2.30GHz; Chrome/153.0.8010.52; hardware ANGLE/Vulkan browser launch. Guest audio remains enabled; browser output is muted during scripted checks.

| # | Frozen build | WASM SHA-256 |
| ---: | --- | --- |
| 1 | baseline | `e715734a51b115f529727070a56147124b7544ac1f2ef1245aa98eb1e8ccfa67` |
| 2 | intermediate | `9886fe97077f6d1773d83319c4a799816e5c07b5ff08fa2b41196f670d369d8e` |
| 3 | candidate | `176bef9bbb8f0899f8538b60978a0674a30e95b9cec32ddb015dc1cd10cd39ca` |

Raw evidence: `/home/claude/.scratch/eka-compiler-startup-20261012`. Startup folders contain reports, phase counters, milestone logs, screenshots and renderer CPU accounting. `summary.json` retains every startup observation, including the intermediate run. Runtime scripts and route steps are retained beside the captures.

```sh
cmake --build build-wasm --target eka2l1_wasm test_state_liveness test_source_maps test_aot_wasm -j4
node build-wasm/src/tests/aot/test_state_liveness.js dump
node build-wasm/src/tests/aot/test_source_maps.js output.wasm
node build-wasm/src/tests/aot/test_aot_wasm.js
cmake --build build --target eka2l1_qt ekatests eka_cpu_tests -j4
ctest --test-dir build --output-on-failure
node src/tests/wasm/startup.mjs https://claude-laptop.lan:8188/ /absolute/path/to/new-output hooked-on 180
```

The previous [startup profile](SWI_FILE_HASH_BYPASS_RESULTS.md) identified `state_local_cache::finish` and compiler allocations as hot. Its sampled attribution includes inlined work and profiler disturbance; those percentages are not treated as directly recoverable wall time.
