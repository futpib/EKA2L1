# Asphalt 3 memory attribution after the upstream merge (2026-10-16)

Asphalt 3 was the largest game in the latest supported-game memory comparison. This fresh capture used `wasm-port` at `1f2ba0c69`, the persistent-device browser path, stock `SYM.ROM` (SHA-256 `89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490`), and Chrome 153.0.8010.52. The production and allocation-instrumented runs both reached the racing HUD without guest errors. A third build with guest memory events enabled reached the same HUD and continued five seconds of guest time. All three used isolated local servers; the `.lan` release was unchanged.

Captures are in `/home/claude/.scratch/eka-memory-current-20261016/{control-run,diagnostic-run,guest-trace-race-run-2}`. Their WASM SHA-256 hashes are respectively `9b99619650d6208183a3a94b51b100ac4ab1bf39be78e16d7a1f325a42a48a2d`, `ca9272519ec8234c1d5814fe0a8d920fe2a0576149fa941d7a45376418d0bf40`, and `1595f717ffe239fd9bcfa9abdb77347053417472e5db1422330c331c521167fd`. The temporary route hook used for the guest-event capture was removed afterward.

After profiling, both diagnostic build flags were reset to `OFF` and the WASM target rebuilt. The rebuilt binary has a different hash (`08b851ee32134e2727548bca00bc84729d9731d4ae547cd425be7e7cbdc77748`), so it was independently run through the same Asphalt 3 racing-HUD route: zero errors and 3,014.98 MiB endpoint browser PSS, within 0.9% of the original production capture. The detailed process mappings below belong to the original production capture, not this restore check.

The production endpoint used **3,041.43 MiB whole-browser PSS**, with a sampled peak of 3,118.23 MiB during the route. The 1,709.5 MiB WASM linear-memory *capacity* is virtual capacity, not resident memory. Mapping PSS sums to 3,040.06 MiB; its 1.37 MiB difference from the separately sampled process rollups is from reading active processes at different instants.

| # | Production browser mapping | PSS, MiB | What the capture establishes |
|---:|---|---:|---|
| 1 | Resident WASM linear memory | 1,012.00 | C++ owner evidence comes from a separate diagnostic build below. |
| 2 | Chromium PartitionAlloc | 956.79 | Native allocation samples name V8 module construction, but do not partition retained pages. |
| 3 | V8 executable code and trampolines | 434.65 | 392.82 MiB of present pages intersect guest-region labels in an append-only V8 perf map. |
| 4 | File-backed mappings | 431.93 | Chromium and other files across the process tree. |
| 5 | Other V8 sandbox mappings | 111.71 | No precise emulator owner established. |
| 6 | Other V8 mappings | 51.91 | No precise emulator owner established. |
| 7 | Other anonymous/shared mappings | 41.06 | No precise emulator owner established. |

The allocation profiler recorded 33,962,870 allocations with **zero dropped records, unknown frees, or truncated stacks**. It reports 1,484.92 MiB live; after accounting for four bytes per live allocation, the `mallinfo` reconciliation residual is 1.17 MiB while the game is active. Its diagnostic overhead and different scheduling make its browser PSS and FPS unsuitable for a production comparison. Whole-page residency was verified against the Chrome renderer's WASM backing via a marker in `/proc/<pid>/pagemap`.

| # | Diagnostic C++ owner | Live, MiB | Whole-page resident, MiB | Allocation site |
|---:|---|---:|---:|---|
| 1 | Per-process direct arenas | 832.00 | 278.00 | `src/emu/mem/src/model/multiple/process.cpp:53` |
| 2 | Guest chunk backing | 349.01 | 349.00 | `src/emu/mem/src/model/multiple/chunk.cpp:380` through `common::map_memory()` |
| 3 | Small allocations below the 64 KiB unwind threshold | 101.09 | Unknown | Allocator counts bytes but omits stacks. |
| 4 | ROM/mapped-file bytes | 66.71 | 66.71 | `src/emu/common/src/virtualmem.cpp:312`; live mapped ROM, not the old duplicate WASMFS buffer. |
| 5 | Worker stacks and thread storage | 42.03 | 39.95 | Pthread startup and `src/emu/wasm/main.cpp`. |
| 6 | WASMFS file storage | 32.05 | 31.80 | File buffers; the previous profile had 160.05 MiB live before the ROM-handle fix. |
| 7 | Emulated OS services | 30.97 | 30.89 | Includes five 4 MiB window drawing buffers in `src/emu/services/src/window/classes/winuser.cpp`. |
| 8 | ROM/executable loading | 11.22 | 11.11 | Loader and process construction. |
| 9 | ARM compiler and code cache | 9.58 | 9.55 | Includes the 8 MiB flat translation page table. |
| 10 | Other large allocations | 6.02 | 5.96 | Mixed owners. |
| 11 | ARM CPU state | 4.26 | 4.21 | Guest thread CPU contexts. |

The **strongest demonstrated over-retention** is two maximum-size guest chunks. The current guest event trace identifies their owners and committed bytes both when the racing HUD first appears and five seconds of guest time later. The independent allocation capture finds their full host backings resident.

| # | Chunk | Host backing resident | Guest committed | Resident above commitment |
|---:|---|---:|---:|---:|
| 1 | `AknIconSrv` local chunk, guest base `0x01700000` | 128.00 MiB | 4 KiB | 127.996 MiB |
| 2 | FBS `FbsLargeChunk`, guest base `0x40400000` | 96.00 MiB | 2,428,928 bytes (2.316 MiB) | 93.684 MiB |
| 3 | Both | 224.00 MiB | 2.320 MiB | **221.680 MiB** |

The emulator initializes `committed_` to zero in `multiple_mem_model_chunk::do_create()` and then obtains backing for `max_size_` at `src/emu/mem/src/model/multiple/chunk.cpp:365-381`. In this WASM build, those two separate backings have every whole page resident. Guest demand reaches only the committed amounts above. This proves a discrepancy at the current scene, **not** that a proposed sparse implementation would save exactly 221.68 MiB of browser PSS without overhead or regression.

The 128 MiB request comes from guest ARM code. The earlier [`chunk_new` bridge trace](ASPHALT3_PERSISTENT_MEMORY_PROFILE.md) for this exact ROM recorded process `AknIconSrv`, guest PC `0x8019DF64`, and LR `0x801A85B7`; its current event trace again assigns the 128 MiB chunk to `AknIconSrv`. The ROM bytes at that handoff disassemble as `EUser.dll 0x801A85B2: blx 0x8019DF60`, `0x8019DF60: svc #0x6A`, `0x8019DF64: bx lr`. `src/emu/kernel/src/svc.cpp:1451` handles `chunk_new`, registered as SVC `0x6A` for this firmware at line 6460. The precise caller instruction within `AknIconSrv.exe` has not been unwound. The FBS request is different: host C++ creates it at `src/emu/services/src/fbs/fbs.cpp:362-374`; there is no single guest ARM allocation instruction for that chunk.

There are also smaller non-direct chunks with uncommitted capacity, including `FbsSharedChunk` (2 MiB backing, about 0.12 MiB committed) and many 1 MiB code-region reservations. They account for some of the remaining 125 MiB in the 349 MiB chunk-backing owner. Their current commitment suggests another possible saving, but this capture does not prove every page can be reclaimed while preserving future guest mapping, aliases, pointer stability, and throughput. The 13 direct arenas reserve 832 MiB of allocator address space but only 278 MiB is resident; their reservation is not an 832 MiB physical leak. They have 192 MiB initialized prefixes at the endpoint.

The browser's 956.79 MiB PartitionAlloc and 434.65 MiB executable mappings are larger than either chunk, but this profile does **not** establish their avoidable share. Native sampling attributes an estimated 329.1 MiB to V8 copying module input bytes, corresponding to `src/emu/cpu/src/aot/aot_runtime.cpp:654-657`, plus 102.5 MiB to `WasmCode` construction and 63.3 MiB to trap-handler registration. Those are allocation-sample estimates, not retained-byte measurements. The route ended with 140,200 compiled guest functions, of which 127,369 were demand compiled. The V8 perf map is append-only, so its guest-labelled resident pages are approximate and cannot reliably assign live code memory to an individual Symbian DLL.

A focused next experiment should remove eager physical backing for **non-direct** uncommitted chunk pages while keeping guest zero-fill and stable mapped pointers, then repeat the same racing-HUD browser-PSS, allocation-residency, and unpaced-throughput captures. The measured 221.68 MiB is the first target; the smaller chunks and V8 module footprint need separate proof before treating them as savings.
