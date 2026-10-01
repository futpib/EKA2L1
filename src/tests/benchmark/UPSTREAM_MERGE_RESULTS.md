# Upstream merge and new deterministic baseline

Source: `5f54a684f17733c618c8d536c26273d4be01d010` on local `wasm-port`.
Merged upstream `EKA2L1/EKA2L1` master `dd3e2219f561d5f938cbd78c606980bbd5cd4723`, containing 354 commits absent from the branch. Nothing was pushed. The single checkout is `/home/claude/code/EKA2L1`; previous paths are compatibility symlinks for existing CMake caches, not linked worktrees.

## Integration

The merge retains the browser backends, AOT regions, exact code-byte/mapping validation and deterministic guest clock. Upstream's retained interpreter cache and branch optimizations are enabled. ARM/Thumb mode now participates in decoded-cache and successor-link keys; a regression test executes both modes at the same address across context loads.

Upstream bulk-loop acceleration is excluded from benchmark and AOT execution: it can exceed an exact instruction quantum and does not materialize every intermediate register state. This preserves the deterministic contract while leaving its normal upstream configuration available. Fused compare/branch execution still counts and profiles each guest instruction separately.

New timezone APIs retain fixed UTC in benchmark mode. The deterministic audio backend follows upstream's interleaved sample-count and retryable callback contracts, including callback destruction safety. Audio quality remains deferred. Browser certificate verification fails closed because a system trust store is unavailable; guest socket networking remains disabled. Upstream test platform stubs replace the duplicate local stubs.

## Correctness

Both complete native repeats and both AOT browser repeats match exactly across 1,000 images, guest timestamps/instruction counts, PCM and audio events. One browser repeat uses interpreter checking every 1,024 compiled blocks; its sampled calls use instrumented memory helpers. Inline memory is covered by differential tests and replay comparison.

- First captured image: 21.031559 guest seconds.
- Last: 70.969054 guest seconds, 10,169,792,663 guest instructions.
- Captured gameplay: 49.937495 guest seconds, 20.005 changing images per second.
- All 1,000 full images and 3D viewports are distinct; minimum adjacent viewport change is 21.98%.
- Audio: 3,406,514 stereo PCM frames at 48 kHz, identical event logs.
- An additional 85-image browser interpreter run matches the new native reference exactly.

**This is a new upstream baseline.** It differs from the previous reference from image zero, including guest records and pixels. The previous artifacts are preserved. Do not compare pre-merge and post-merge wall times as an identical-work speedup: upstream also changes guest patches, services and timer behavior.

All native CTest targets pass: 315 package cases (28,468 assertions), 25 CPU cases (239 assertions), and two network cases (35 assertions). The seven previously reported failures are absent. WASM passes 127 tests, including 280,672 bounded state/memory comparisons and 46,080 long-multiply comparisons; its pre-existing documented harness XFAIL remains. Seven frontend smoke tests pass.

Build and unit-test logs record pre-merge HEAD `233caada` plus the staged merged source; that source was committed as `5f54a684f` without subsequent implementation edits before the full replays and timing trials. Browser/native binary hashes and run metadata are in the evidence JSON.

## Physical-GPU measurements

Serial order: interpreter, AOT, AOT, interpreter. Each trial runs guest time 21–25 seconds, with capture/readback and detailed timing counters disabled, rendering enabled, no verifier, and identical guest instruction/presentation totals. Other owned correctness/build jobs had finished. Renderer: ANGLE Vulkan, NVIDIA Quadro T1000 with Max-Q Design. This remains a shared host.

| Mode | Host seconds for four guest seconds | Mean throughput |
| --- | --- | --- |
| Merged interpreter | 7.71275, 7.67578 | 0.520x realtime |
| Merged AOT regions (mode 5) | 4.85504, 4.86024 | 0.823x realtime |

AOT is about 1.58x the merged interpreter in this window. Another 21.4% throughput is required to reach 1x here; sustained interactive playability and input latency are not yet established.

A separate CPU-sampled AOT run takes 5.18521 host seconds. Of its 5.4444-second guest-worker sampled span, generated code and callees occupy 48.73%, compiled lookup self-time 14.88%, interpretation/dispatch 12.49%, exact SIMD byte comparison 5.15%, and `memcmp` 2.40%. The page is 91.05% idle. These percentages include overlapping categories and are not additive wall-time savings.

Next experiment: reduce redundant region exit/budget checks. A previously rejected grouped-budget prototype increased interpreter fallback cost; upstream cache retention changes that tradeoff. Retest it with exact-budget comparisons and controlled timing before retaining it. Realtime work remains active.

## Reproduction and artifacts

Use the build/replay commands in [README.md](README.md), region mode `EKA2L1_BENCHMARK_AOT=5`, and optional `EKA2L1_AOT_VERIFY=1024`. The raw artifacts are under `/home/claude/.scratch/eka-benchmark/`:

- `upstream-native-full/run-{0,1}/frames`
- `upstream-wasm-full` and `upstream-wasm-full-checked`
- `upstream-wasm-interpreter` (85-image comparison)
- `upstream-timings` and `upstream-cpu`
- `upstream-merged-build` (archived browser binary/loader/data and hashes)

Timing uses `EKA2L1_GPU=hardware EKA2L1_PROFILE_DETAIL=0`, `profile.ts ASSETS OUTPUT 2 0`, with AOT modes 0 and 5. CPU sampling changes the final argument to 1. Warm fixtures pause at the same guest boundary and are released serially by `profile_batch.py`. All recorded results and hashes are in [UPSTREAM_MERGE_EVIDENCE.json](UPSTREAM_MERGE_EVIDENCE.json).
