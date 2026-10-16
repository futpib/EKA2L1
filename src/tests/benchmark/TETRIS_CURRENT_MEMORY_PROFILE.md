# Current Tetris browser memory profile (2026-10-16)

Tetris 4.21.41 on the stock Nokia 5320 firmware was the largest of the current
gameplay captures: 2,863.28 MiB whole-browser proportional set size (PSS) at
its marathon board, versus 2,801.24 MiB for Asphalt 3 at its racing HUD and
2,390.97 MiB for Hooked On at its boat scene. These are measured scenes, not
all possible game states. Each used a fresh Chromium 153 browser, the
persistent-device/OPFS firmware path, and the default release WASM build
`dba61b5a7f1a470f3b7bcc1293ef1d0ed79b0b870f54fe5357fb5629111b714e`
on `.lan`. The Tetris route reached moving pieces and produced no guest errors.
The Tetris production capture is
`/home/claude/.scratch/eka-memory-latest-tetris-20261016`.
The active renderer contributed 2,298.35 MiB PSS; the browser, GPU, storage,
network, and other renderer processes contributed the remaining 564.93 MiB.
The highest once-per-second PSS sample during this route was 2,937.78 MiB;
the tables describe the steady board endpoint, not that sampled peak.

The table is a **partition of measured PSS**, across every Chromium process
in that fresh browser. It sums exactly to 2,863.28 MiB after rounding. The
last row reconciles `smaps` with the separately sampled process totals; the
measurements are sequential, not an instantaneous system-wide snapshot. The
1,450.19 MiB WASM *capacity* is reserved address space and must not be added.
PSS covers process-addressed system RAM; it does not include GPU VRAM or the
kernel page cache.

| # | Browser memory owner | PSS, MiB | What the evidence identifies |
|---:|---|---:|---|
| 1 | Chromium PartitionAlloc mappings | 949.65 | Resident native allocator mappings across browser processes; sampled live allocation origins below, but no complete allocation-to-mapping split. |
| 2 | WASM linear-memory backing | 843.40 | Resident pages containing emulator C++ and guest memory; separate diagnostic breakdown below. |
| 3 | File-backed Chromium mappings | 428.84 | Shared libraries, browser binary, and mapped files across processes; not all private RAM or emulator-owned data. |
| 4 | V8 executable code and trampolines | 426.86 | Native JIT code; a separate perf-map capture labels about 394 MiB of present renderer pages as generated guest code, with possible stale labels. |
| 5 | Other V8 sandbox mappings | 115.54 | Browser-owned memory with no trustworthy C++ allocation site in this capture. |
| 6 | Other V8 mappings | 56.13 | Browser-owned memory with no trustworthy C++ allocation site in this capture. |
| 7 | Other anonymous/shared mappings | 41.77 | Remaining browser-process mappings. |
| 8 | Process-sampling reconciliation | 1.09 | `smaps_rollup` process PSS minus the sum of individual `smaps` mappings. |
| 9 | **Total browser PSS** | **2,863.28** | Sum of rows 1-8. |

For C++ source attribution I rebuilt the same source with
`EKA2L1_WASM_ALLOCATION_PROFILE=ON`, then ran the same persistent-device Tetris
route. The raw capture and source-mapped summary are in
`/home/claude/.scratch/eka-memory-latest-tetris-allocation-20261016` and its
frozen WASM build is in
`/home/claude/.scratch/eka-memory-latest-tetris-allocation-build-20261016`.
The diagnostic WASM SHA-256 is
`a702e869a5af2f701e72ec31a18c199ddea8553274d27d8d9eee0d5944c91c08`.
Its allocator recorded 35,255,424 allocations with no dropped events,
unknown frees, or truncated stacks. It counted 1,173.31 MiB live allocator
payload versus 1,175.68 MiB from `mallinfo`, including per-block metadata.
That payload includes large virtual reservations, so it does not partition
resident RAM. The diagnostic build also has different overhead and compilation
timing: its WASM mapping was **880.00 MiB resident**, rather than the
production build's 843.40 MiB. The following table partitions only that
diagnostic mapping, using Linux pagemap to assign whole resident pages inside
source-attributed live allocations. Boundary pages and everything outside
such allocations stay in the explicit residual.

| # | Diagnostic WASM owner | Resident whole pages, MiB | C++ origin or limitation |
|---:|---|---:|---|
| 1 | Process arenas | 366.00 | `src/emu/mem/src/model/multiple/process.cpp:52`; thirteen stable 48 MiB reservations, 624 MiB total virtual size. |
| 2 | Guest chunk backing | 135.00 | `src/emu/mem/src/model/multiple/chunk.cpp:403`; includes AknIconSrv's 128 MiB sparse reservation. |
| 3 | ROM and mapped files | 66.71 | `src/emu/common/src/virtualmem.cpp:312` via WASMFS `mmap`; one live ROM mapping. |
| 4 | Worker stacks and thread storage | 39.95 | `src/emu/wasm/main.cpp:158`, pthread startup, and graphics/CPU workers. |
| 5 | Emulated OS services | 29.48 | Includes five retained 4 MiB window redraw allocations through `src/emu/services/src/window/classes/gstore.cpp:204` and `winuser.cpp:1576`, plus roughly 8.3 MiB of font data through `src/emu/services/src/fbs/impls/font.cpp:1146` and `:1160`. |
| 6 | WASMFS file storage | 16.14 | `wasmfs::DataFile::Handle::write`; distinct from the mapped ROM allocation. |
| 7 | ROM and executable loading | 10.68 | Loader and process creation stacks. |
| 8 | ARM compiler and code cache | 9.55 | Includes the 8 MiB page table managed by `src/emu/cpu/src/aot/memory_experiment.cpp:13`; the captured stack passes through the CPU loop at `src/emu/system/src/epoc.cpp:844`. |
| 9 | Other large allocations | 5.88 | Mixed source-mapped stacks. |
| 10 | ARM CPU state | 4.95 | Per-thread emulated CPU contexts. |
| 11 | Unassigned WASM resident pages | 195.66 | Includes allocations under the 64 KiB stack-unwind threshold (102.38 MiB live payload), FBS backing obtained with `sbrk` at `src/emu/services/src/fbs/fbs.cpp:75`, allocator metadata/free pages, and boundary pages. These components are not individually quantified by this capture. |
| 12 | **Total diagnostic WASM mapping PSS** | **880.00** | Sum of rows 1-11; do not add this table to the production-browser table. |

The separate guest-memory trace, using
`EKA2L1_MEMORY_ACCESS_PROFILE=ON` on the same persistent-device route, is in
`/home/claude/.scratch/eka-memory-latest-tetris-guest-20261016`. It reached
the Tetris board and ran another 15 guest seconds/456 presentations with no
errors. At its endpoint, AknIconSrv had requested a 128 MiB local chunk at
guest address `0x01700000` but committed only **4 KiB**. The diagnostic
allocator grouped that chunk with two smaller allocations into 130 MiB of
virtual backing. Pagemap measured **2 MiB resident in the 128 MiB AknIconSrv
allocation**, plus 1 MiB in each smaller allocation: 4 MiB for the group. The
guest request crosses `chunk_new` at `src/emu/kernel/src/svc.cpp:1451` and
the sparse host allocation at `src/emu/mem/src/model/multiple/chunk.cpp:403`.
An [earlier trace on this exact ROM](ASPHALT3_PERSISTENT_MEMORY_PROFILE.md)
identified its EUser.dll ARM/Thumb SVC wrapper at `0x8019DF60` (`svc #0x6A`,
host bridge PC `0x8019DF64`); the current event recorder does not itself
capture the guest PC or the caller inside AknIconSrv.exe.

The global FBS large chunk is 96 MiB virtual, **2.316 MiB guest committed**
at the Tetris endpoint. Its fresh `sbrk` backing intentionally bypasses the
malloc lifetime profiler, which is why its residency is in the residual, not
the 135 MiB guest-chunk row. The emulator reports 176 MiB of initialized
process-arena prefix across the live processes, whereas 366 MiB of arena
whole pages are resident. This 190 MiB difference is a high-priority
attribution gap: reused dirty allocator pages, direct host writes, and arena
initialization are not separated here. Six of the thirteen 48 MiB reservations
have all their pages resident. It is not yet a proven 190 MiB saving.

A separate production-build browser allocation capture in
`/home/claude/.scratch/eka-memory-latest-tetris-full-20261016` sampled about
322 MiB of still-tracked native allocations originating at V8's
`GetAndCopyFirstArgumentAsBytes` while it consumes generated module bytes.
The bridge at `src/emu/cpu/src/aot/aot_runtime.cpp:652-657` first copies
those bytes into a `Uint8Array`, then calls `new WebAssembly.Module`, which
makes its own native copy. The same sample had about 101 MiB originating at
`WasmCode::WasmCode` and 64 MiB at trap-handler registration. Chromium's
sampling heap profiler removes samples when their allocations are freed, so
these are **estimates of allocations still tracked at the capture endpoint**,
not cumulative allocation churn. Sampling began after browser startup, misses
some allocations, and does not identify which virtual-memory mapping holds
each sampled pointer; these estimates cannot be added to PSS or subtracted
directly from the 949.65 MiB PartitionAlloc row. That capture had 145,793
compiled functions and a 435.79 MiB executable mapping; approximately 393.93
MiB of present renderer code pages intersected guest-region labels in the
append-only V8 perf map. Generated ARM-in-WASM code and its browser
bookkeeping are thus the largest identifiable non-guest-payload pressure, but
the exact avoidable portion of PartitionAlloc still needs a pointer-to-mapping
retained-allocation census.

## Follow-up: remove the JavaScript module-input copy

The AOT bridge now passes its view of the generated bytes in WASM linear
memory directly to `new WebAssembly.Module`. The constructor still takes its
own native copy; the change removes only the extra `new Uint8Array(wasmBytes)`
allocation. The frozen control was the previous `.lan` release
`dba61b5a7f1a470f3b7bcc1293ef1d0ed79b0b870f54fe5357fb5629111b714e`;
the candidate WASM SHA-256 was
`02d1a3070daa59c2abf12dff69fb7b291bda9eeb4db6b64f2ed0fd71d94417e2`.
Each used a fresh Chromium 153 browser and the same persistent-device Tetris
marathon-board route. All four runs reached moving pieces with zero guest
errors. Raw captures are under
`/home/claude/.scratch/eka-no-copy-{control,candidate}{,-full}-20261016`.

| # | Run | Whole-browser PSS, MiB | PartitionAlloc PSS, MiB | WASM backing PSS, MiB | V8 executable PSS, MiB | Observed peak PSS, MiB |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Control, normal | 2,789.99 | 938.35 | 787.04 | 415.61 | 2,881.08 |
| 2 | No JS copy, normal | 2,831.48 | 923.96 | 834.90 | 429.36 | 2,882.08 |
| 3 | Control, native/V8 profilers | 2,882.25 | 952.37 | 844.63 | 434.57 | 2,925.89 |
| 4 | No JS copy, native/V8 profilers | 2,892.28 | 958.24 | 842.97 | 433.28 | 2,938.47 |

The profiler captures sampled still-tracked native allocations at V8's
`GetAndCopyFirstArgumentAsBytes` at 322.66 MiB with the JS copy and 321.83 MiB
without it. `WasmCode::WasmCode` remained 101.31 versus 101.84 MiB, and
trap-handler registration 65.41 versus 65.11 MiB. These samples are estimates,
not PSS partitions. Removing our temporary copy did **not** remove V8's
retained module bytes or yield a measured steady-state or one-second-sampled
peak RAM reduction. Normal-run whole-browser PSS varied by 41.49 MiB in the
opposite direction while the observed peaks differed by only 1.00 MiB; the
different mapping movements and diagnostic-run totals make that endpoint
difference unsuitable as a causal cost of the two-line change. The change
avoids one full module-byte copy during installation, but a subsecond
allocation-peak or startup-time benefit was not established by this capture.
The candidate passed all 197 standalone AOT WASM correctness checks. A Firefox
headless Snakes check could not reach guest execution: its WebGL2 context
creation failed during graphics initialization. It supplies no Firefox
gameplay validation; Chromium supplied the real-browser gameplay validation
above.
