# Current Asphalt 3 browser memory profile

On `wasm-port` commit `b2cdad1f2`, the `.lan` release with WASM SHA-256
`490322b0528ec2645253785e53147ae98b5029a4d4363a3b2744fbbf77766b7d`
ran stock Nokia 5320 firmware in Chromium 153.0.8010.52. Both comparison
routes used the persistent OPFS device, completed the 305-file firmware
offload, and reached actual gameplay without guest errors. Asphalt 3's race
endpoint retained **2,893.39 MiB** of whole-browser proportional set size
(PSS), versus **2,857.22 MiB** at the Tetris marathon board. The sampled
route peaks were 2,905.81 and 2,932.20 MiB respectively: Tetris had the
higher transient peak, but Asphalt 3 retained more at the endpoint. Earlier
catalogue captures put other supported games below these two; they were not
rerun on this binary. The Asphalt 3 route includes sound and five seconds of
unpaced racing; the Tetris route includes 15 seconds on the board. Tetris's
sampled peak occurred while moving from its client UI toward the game menu,
before the marathon-board measurement.

## Production browser PSS

This is the **uninstrumented** Asphalt 3 capture at the race endpoint. Linux
`smaps` assigns resident PSS to each mapping in every Chromium process, and
`smaps_rollup` supplies the process total. The small reconciliation row is
sampling skew while the processes were read sequentially. PSS excludes
unmapped disk page cache, external GPU VRAM, and other applications; it is
the browser's measured CPU-memory share, not a phone-RAM estimate. The WASM
linear memory's capacity was 1,469.25 MiB; only its resident PSS is in the
table.

| # | Mapping owner | PSS, MiB | Attribution |
|---:|---|---:|---|
| 1 | Chromium PartitionAlloc | 922.53 | Browser-native allocator mappings; sampled origins below cannot be assigned to this row byte-for-byte. |
| 2 | WASM linear memory | 814.00 | Emulator C++, guest memory, ROM and WASMFS; separate source-mapped diagnostic below. |
| 3 | V8 executable code and trampolines | 500.87 | Mostly generated guest ARM translations; 430.65 MiB of resident pages intersect guest labels in the separate profiler capture. |
| 4 | File-backed mappings | 450.27 | Chromium libraries and other mapped files; not all emulator data. |
| 5 | Other V8 sandbox mappings | 115.17 | Browser-owned mappings; no reliable C++ allocation site. |
| 6 | Other V8 mappings | 48.74 | Browser-owned mappings; no reliable C++ allocation site. |
| 7 | Other anonymous or shared mappings | 40.91 | Stacks and miscellaneous browser mappings. |
| 8 | Process-sampling reconciliation | 0.90 | `smaps_rollup` minus the mapping reads. |
| 9 | **Total browser PSS** | **2,893.39** | Sum of rows 1–8. |

A separate **instrumented production-binary** capture reached the same HUD
at 2,938.31 MiB PSS. Its V8 perf map and pagemap found 110,247 present
executable pages, or 430.65 MiB, intersecting generated `f_`, `r_` or `s_`
guest-translation labels. The append-only perf map can retain stale labels
after code-address reuse, so this is strong ARM attribution, not an exact
live allocation partition. One labelled guest PC is `0x74e37c00` in
`asphalt3.exe`, whose runtime code begins at `0x74e00000`. The map logged
8,574 translations in that 1 MiB guest-code slot, totalling 47.77 MiB of
native *code lengths* over the capture; this is not its endpoint resident PSS.
The ordinary race endpoint reported 140,278 compiled functions and 7,069
retired functions.

Chromium's native lifetime sampler attributed about **331.0 MiB** of
still-sampled allocations to V8's `GetAndCopyFirstArgumentAsBytes`,
**109.1 MiB** to `WasmCode` construction and **71.9 MiB** to trap-handler
registration. [`js_instantiate_aot_module`](../../emu/cpu/src/aot/aot_runtime.cpp)
passes a `Uint8Array` *view* of the C++ buffer to `new WebAssembly.Module` at
line 655. The extra JavaScript copy has already been removed; V8's native
module-input copy remains. The sampler started after `Module.calledRun`, so
it omits initial runtime allocations. Sample estimates do not identify the
mapping containing each pointer and **must not be added to PSS**. Their
large size and the executable-page labels identify generated WASM code as
the largest plausible area for reducing browser memory, but they do not
prove that the live modules or their code can simply be freed.

## C++ and guest allocation origins

An isolated build of the **same source** enabled
`EKA2L1_WASM_ALLOCATION_PROFILE` and `EKA2L1_MEMORY_ACCESS_PROFILE`. Its
WASM SHA-256 is
`d08de679dc5b477c00cb5cfff6bffe0d0d8fc0ac2609f251b54b3b3107898e08`.
It used the persistent device but had no compatible derived ROM-code cache.
It reached the Asphalt 3 racing HUD without errors. The allocator recorded
33,977,877 allocations, zero dropped events, unknown frees or truncated
stacks. Its 1,181.33 MiB live allocator payload includes *virtual* arena and
chunk reservations; it is not physical RAM. Pagemap assigned whole resident
pages to live allocations at the route endpoint. This diagnostic browser
used 3,105.85 MiB PSS and its WASM mapping used **908.00 MiB**; neither
number is a production-memory measurement.

| # | Diagnostic WASM owner | Resident whole pages, MiB | C++ origin or limit |
|---:|---|---:|---|
| 1 | Per-process direct arenas | 340.00 | [`multiple_mem_model_process::direct_local_backing`](../../emu/mem/src/model/multiple/process.cpp) line 52. |
| 2 | Guest chunk backing outside arenas | 127.62 | [`multiple_mem_model_chunk::do_create`](../../emu/mem/src/model/multiple/chunk.cpp) line 403. |
| 3 | ROM and mapped files | 66.71 | [`common::map_file`](../../emu/common/src/virtualmem.cpp) line 312; one live ROM mapping. |
| 4 | Worker stacks and thread storage | 39.95 | Pthread startup via [`ensure_system_started`](../../emu/wasm/main.cpp) line 158. |
| 5 | WASMFS file storage | 31.82 | `wasmfs::MemoryDataFile::write`; retained ordinary file data. |
| 6 | Emulated OS services | 29.74 | Includes 20 MiB in five redraw-builder allocations via [`redraw_msg_canvas::draw`](../../emu/services/src/window/classes/winuser.cpp) line 1576, plus font data. |
| 7 | ROM and executable loading | 11.09 | Guest loader and process construction stacks. |
| 8 | ARM compiler and C++ code cache | 9.65 | Includes the 8 MiB page table in [`memory_experiment.cpp`](../../emu/cpu/src/aot/memory_experiment.cpp). |
| 9 | Other large allocations | 5.96 | Mixed source-mapped stacks. |
| 10 | ARM CPU state | 4.21 | Per-thread emulated CPU contexts. |
| 11 | Unassigned WASM pages | 241.25 | Includes 101.95 MiB live payload in allocations below the 64 KiB unwind threshold, profiler static storage, FBS backing obtained with `sbrk`, free/allocator pages and block boundaries; their individual physical shares are not measured. |
| 12 | **Diagnostic WASM mapping PSS** | **908.00** | Sum of rows 1–11; do not add to the production table. |

The 13 live direct arenas reserve 624 MiB of WASM address space and account
for 340 MiB resident. [`initialize_direct_range`](../../emu/mem/src/model/multiple/process.cpp)
has explicitly cleared and published 160 MiB of prefixes. The guest event
trace found 85 active direct chunks with 137 MiB maximum size but only
**10.13 MiB currently committed**. Thus prefix clearing touches far more
memory than the current guest commitment, while arena residency exceeds even
the cleared prefix by 180 MiB. Previously touched allocator pages may be
reused for a newly reserved arena, and the trace does not distinguish that
from later host writes. The 340-versus-10 MiB gap is a measured
host-to-guest mismatch, **not** a safe 330 MiB saving: current commitment
does not cover earlier writes or direct-pointer and zero-fill semantics.

The largest sparse guest request is the `AknIconSrv` local chunk: **128 MiB
maximum, 4 KiB guest committed and 2 MiB resident** in its individual
allocation. Its host stack runs through guest `chunk_new` at
[`svc.cpp`](../../emu/kernel/src/svc.cpp) line 1451 and sparse allocation at
[`chunk.cpp`](../../emu/mem/src/model/multiple/chunk.cpp) line 403. The
current event recorder identifies the guest process, address `0x01700000`
and SVC bridge but does not unwind the caller PC inside `AknIconSrv.exe`.
The global FBS large chunk reserves 96 MiB but has only 2.316 MiB guest
committed; it receives stable `sbrk` backing at
[`fbs.cpp`](../../emu/services/src/fbs/fbs.cpp) line 75 and lies in the
unassigned row. Both reservations are mostly nonresident, so reservation
size alone is not evidence of a current RAM leak. Asphalt 3's own 16 MiB
heap chunk has 7.961 MiB guest committed.

The five 4 MiB redraw allocations remain live from the language screen
through the race endpoint. They are a smaller, concrete retention candidate,
but the profile cannot establish whether their buffers remain useful to
later frames. The 66.71 MiB ROM mapping is entirely resident; changing that
would require a demand-paged ROM representation, not just deleting a C++
allocation. No single guest ARM allocator call explains the 2.89 GiB browser
total: V8's translation code and metadata, host arenas, OS services and
Chromium itself have distinct owners.

Raw normal captures:
`/home/claude/.scratch/eka-memory-20261017-current/asphalt-3/` and
`tetris/`. Browser sampling and native symbols:
`asphalt-3-full/browser-memory-attribution.json`. Diagnostic lifetimes and
guest events: `asphalt-3-diagnostic/allocation-attribution.json` and
`asphalt-3-guest/profile-final.json`. The matching Chromium debug ELF has
build ID `9edff2492605d22e9fdebd338b9713367b83286b`.
