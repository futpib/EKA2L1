# Software page-cache comparison

The software TLB pays for itself in these workloads. Original-index TLB-only had the best mean: **39% higher wall throughput in Snakes and 31% in Sky Force than no software page caches**. The current two-cache configuration gained **26% and 25%**. Adding more cache machinery did not produce a consistent improvement.

All eight configurations use one frozen experimental WASM binary based on `91427bdd651fcc5f1578f19183d2ec19933d44eb`. Production compiler code, defaults and the LAN deployment are unchanged. The experiment is preserved as a [patch](memory_cache_matrix.patch), [serial runner](memory_cache_matrix.py) and [complete measurements](MEMORY_CACHE_RESULTS.json).

## Results

Two retained runs per configuration and game; speedup is mean no-cache seconds divided by mean variant seconds. Higher is better. This is a screening panel, not an established optimum. The raw observations below show the remaining variation.

| # | Configuration | Snakes wall s | Speedup | Sky Force wall s | Speedup |
|---|---|---:|---:|---:|---:|
| 1 | No TLB or last-page cache | 3.197 | 1.000× | 12.819 | 1.000× |
| 2 | Last-page cache only | 2.584 | 1.237× | 11.493 | 1.115× |
| 3 | Original TLB only | 2.301 | 1.389× | 9.800 | 1.308× |
| 4 | Folded TLB only | 2.643 | 1.210× | 10.550 | 1.215× |
| 5 | Original TLB + last-page | 2.479 | 1.290× | 10.985 | 1.167× |
| 6 | Folded TLB + last-page (current policy) | 2.532 | 1.262× | 10.288 | 1.246× |
| 7 | Current caches, older guards | 2.384 | 1.341× | 10.193 | 1.258× |
| 8 | Current caches + span reuse | 2.370 | 1.349× | 11.595 | 1.106× |

The corresponding CPU measurements are for the busiest matched renderer thread. It was named `DedicatedWorker` in every run; its identity was not independently profiled in each run. Renderer-process CPU totals are also preserved in the raw data.

| # | Configuration | Snakes CPU s | CPU speedup | Sky Force CPU s | CPU speedup |
|---|---|---:|---:|---:|---:|
| 1 | No TLB or last-page cache | 2.9032 | 1.000× | 12.0973 | 1.000× |
| 2 | Last-page only; direct page-table walks | 2.2774 | 1.275× | 10.7794 | 1.122× |
| 3 | Original TLB only | 2.0068 | 1.447× | 9.0958 | 1.330× |
| 4 | Folded TLB only | 2.3149 | 1.254× | 9.8239 | 1.231× |
| 5 | Original TLB + last-page | 2.1924 | 1.324× | 10.2981 | 1.175× |
| 6 | Folded TLB + last-page (current policy) | 2.2023 | 1.318× | 9.5623 | 1.265× |
| 7 | Folded TLB + last-page, older guards | 2.0547 | 1.413× | 9.5050 | 1.273× |
| 8 | Folded TLB + last-page + span reuse | 2.0477 | 1.418× | 10.8668 | 1.113× |

Original TLB-only was about **10% faster than the current configuration in Snakes and 5% in Sky Force** by wall time. That is a candidate for a focused production comparison, not a default change made by this experiment.

The additional last-page cache is not a general win over TLB-only. Folded indexing also depends on the surrounding configuration. Older versus compact guards are effectively tied in Sky Force; older guards led the Snakes means. Span reuse improved Snakes over the current policy by about 7% wall throughput, but reduced Sky Force throughput by about 11%. These fresh results do not establish a universally best hash, guard shape or extra cache layer.

## What no cache means

The 512-entry software TLB and generated ARM scalar last-page cache are disabled. Compiled-code caches, the separate executable-mapping cache and compiler-proven invariant memory spans remain enabled in every row. Host CPU caches and V8 optimization remain enabled. Disabling the TLB also affects interpreter accesses.

No-cache ARM and Thumb still execute direct WASM loads/stores. Address translation walks the active authoritative directory/table/page metadata for the 4 KiB geometry used by these assets. Missing mappings, permissions, alignment and endianness retain normal fallback behavior. Disabled TLBs perform no fill, flush or dirty-entry maintenance. A mapped page can be resolved directly even when it would have missed in the finite TLB, so this includes both hit cost and existing miss/fallback cost.

The last-page cache can sit over either the TLB or direct walks. The older-guards row restores the previous scalar guard control flow on the current compiler; it is not a complete historical binary. Span reuse extends cached translations to ARM block/entry spans.

The current-policy row uses the current cache policy inside the experimental binary. All modes share added selection/view-publication code. These numbers do not establish the exact absolute performance of an unmodified production binary or every possible uncached implementation.

## Method and interference

Stock Nokia 5320 ROM/RPKG, stock Snakes and Sky Force packages, fixed recorded input. Snakes runs from 21 to 25 guest seconds: 644,728,231 instructions and 84 presentations. Sky Force combat runs from 42.000001 to 48 guest seconds: 2,171,043,925 instructions and 192 presentations. Startup is excluded. Every timing run has identical guest work and frame journals within its game.

Policy 17, hotpath 2, direct Thumb memory, unsafe-code mode 3, leaf feature mask 128. Custom diagnostics, Chrome tracing and CPU sampling are disabled. Timing uses capture mode 1 (readback and frame metadata, without PNG compression), shared audio and hardware Vulkan on the NVIDIA Quadro T1000. Host: i7-10875H, Chromium 153.0.8010.52, Emscripten 4.0.10, Release build, 32-worker pool.

The initial two serial sweeps ran variants forward then backward. No emulator build, correctness suite, profiler or other owned benchmark overlapped timing. Unrelated Android and native builds did interfere. Four observed overlapping launches are preserved but excluded from the primary aggregates and replaced with the same configurations: three Sky Force runs (last-page-only, original TLB-only, folded TLB-only) and one Snakes last-page-only run. There are 36 total observations and 32 in the primary panel.

A temporary host-process watcher paused only this experiment’s driver between runs during detected build/emulator bursts, resuming after tracked activity stayed below half a core for 60 seconds. It did not stop unrelated work. The timeline and excluded observations are in the JSON. This was not an exclusively reserved machine, and CPU frequency was not fixed. CPU seconds exclude off-CPU time, but not frequency or contention effects. The contended Sky Force last-page-only run consumed 26.80 busiest-thread CPU seconds; its clean replacement consumed 10.81. CPU time alone would not have rescued that comparison.

All renderer-process snapshots are complete, with no counter-read errors. A `ThreadPoolForeg` thread disappeared in three Sky Force snapshots; the dominant `DedicatedWorker` was matched in all of them. The missing threads prevent reconstructing a complete per-thread total for those windows, while process CPU totals remain complete.

## Correctness

The existing AOT suite reports **167 passed, zero unexpected failures**. One documented crash-reproduction harness case remains an expected failure; two diagnostics-only checks are skipped in this diagnostics-free build.

The added matrix passes **129,960 ARM/Thumb cases** across 12 policy/hash combinations, covering exact registers, memory and instruction budgets; permissions, alignment, page crossings and page-zero sentinels; high addresses, aliases and callback state/mapping changes. Additional direct C++ lookup checks observe remapping and permission changes.

Every configuration in both games passes **60 unique native-reference frames**, including every pixel, guest timing/instruction records, PCM and audio event timing. Those 16 correctness replays use the established SwiftShader reference path and are excluded from timing. The timing runs do not save PCM or per-frame PNGs; their frame metadata agrees across all 36 runs.

Additional full-window hardware captures of the current policy match the first 60 native-reference frames in both games. Every final browser screenshot from the timing runs matches either the last or penultimate captured presentation. This confirms the occasional screenshot difference is a presentation offset; browser screenshots are not used as aligned frame oracles.

## Reproduction

Apply `memory_cache_matrix.patch` to a separate source snapshot of the baseline commit. Use the same initialized submodules and `src/tests/wasm` Node dependencies. The patch contains experimental compiler/runtime changes, focused tests and harness controls; do not apply it to production merely to read these results.

With `CACHE_MATRIX_ROOT/source` containing that snapshot and `CACHE_EMSCRIPTEN` pointing to Emscripten 4.0.10:

```sh
cmake -S "$CACHE_MATRIX_ROOT/source" -B "$CACHE_MATRIX_ROOT/build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$CACHE_EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DEKA2L1_BUILD_TESTS=ON \
  -DEKA2L1_BUILD_TOOLS=OFF -DEKA2L1_BUILD_PATCH=OFF \
  -DEKA2L1_ENABLE_SCRIPTING_ABILITY=OFF \
  -DEKA2L1_WASM_DIAGNOSTICS=OFF -DEKA2L1_WASM_DEFER_MEMORY=ON \
  -DEKA2L1_WASM_WORKER_POOL_SIZE=32
cmake --build "$CACHE_MATRIX_ROOT/build" --target eka2l1_wasm test_aot_wasm -j8
node "$CACHE_MATRIX_ROOT/build/src/tests/aot/test_aot_wasm.js"
node "$CACHE_MATRIX_ROOT/build/src/tests/aot/test_aot_wasm.js" --memory-cache-only
```

Run `memory_cache_matrix.py ROOT replays --snakes-assets PATH --sky-assets PATH --reference-root PATH`, then the same command with `timings`. The reference root must contain `replay-standard` and `replay-combat`. The driver refuses existing output directories, records exact commands and binary hashes, and requires identical guest work. `confirm` runs another two sweeps if needed. Run on a quiet host; the one-off process watcher used here is not installed as a service.

Experimental `EKA2L1_MEMORY_CACHE` bits: 1=TLB, 2=scalar last-page cache, 4=older guards, 8=span reuse. Accepted modes are 0, 1, 2, 3, 7 and 11. `EKA2L1_TLB_HASH` selects original (0) or folded (1). These controls exist only in the archived patch.

Artifact root: `/home/claude/.scratch/eka-memory-cache-matrix`. All measured variants use WASM SHA-256 `0fd1e60ad8e357389191f0be7e565fe5057c868453b76145f94f15384a97d366`.

## Retained observations

Each row contains seconds. Replacement observations appear at the end; all four excluded observations remain in the JSON.

| # | Game | Configuration | Wall seconds | Renderer CPU seconds | Busiest-thread CPU seconds |
|---|---|---|---|---|---|
| 1 | standard | none | 3.280720 | 4.010000 | 2.997039 |
| 2 | standard | page_only | 2.619160 | 3.300000 | 2.331776 |
| 3 | standard | original_tlb | 2.220400 | 2.320000 | 1.941860 |
| 4 | standard | folded_tlb | 2.633650 | 2.890000 | 2.295532 |
| 5 | standard | original_both | 2.616540 | 2.850000 | 2.335361 |
| 6 | standard | current | 2.470150 | 2.760000 | 2.142931 |
| 7 | standard | old_guards | 2.460550 | 2.740000 | 2.115925 |
| 8 | standard | span_reuse | 2.348270 | 2.610000 | 2.027419 |
| 9 | combat | none | 12.724100 | 12.750000 | 12.009251 |
| 10 | combat | original_both | 11.756500 | 11.840000 | 11.095740 |
| 11 | combat | current | 10.643800 | 10.520000 | 9.928114 |
| 12 | combat | old_guards | 10.178600 | 10.100000 | 9.465585 |
| 13 | combat | span_reuse | 11.939000 | 11.840000 | 11.149117 |
| 14 | standard | span_reuse | 2.392260 | 2.630000 | 2.067981 |
| 15 | standard | old_guards | 2.307910 | 2.590000 | 1.993541 |
| 16 | standard | current | 2.594060 | 2.910000 | 2.261607 |
| 17 | standard | original_both | 2.341360 | 2.480000 | 2.049471 |
| 18 | standard | folded_tlb | 2.651990 | 2.940000 | 2.334364 |
| 19 | standard | original_tlb | 2.382430 | 2.480000 | 2.071720 |
| 20 | standard | none | 3.112500 | 3.760000 | 2.809352 |
| 21 | combat | span_reuse | 11.250600 | 11.290000 | 10.584400 |
| 22 | combat | old_guards | 10.207400 | 10.150000 | 9.544441 |
| 23 | combat | current | 9.933130 | 9.780000 | 9.196512 |
| 24 | combat | original_both | 10.213200 | 10.160000 | 9.500508 |
| 25 | combat | folded_tlb | 9.830290 | 9.700000 | 9.103563 |
| 26 | combat | original_tlb | 9.748990 | 9.700000 | 9.062613 |
| 27 | combat | page_only | 11.454200 | 11.380000 | 10.746298 |
| 28 | combat | none | 12.914200 | 12.900000 | 12.185351 |
| 29 | combat | page_only | 11.532100 | 11.460000 | 10.812592 |
| 30 | combat | original_tlb | 9.851890 | 9.780000 | 9.129031 |
| 31 | combat | folded_tlb | 11.270200 | 11.240000 | 10.544187 |
| 32 | standard | page_only | 2.549550 | 3.170000 | 2.222987 |
