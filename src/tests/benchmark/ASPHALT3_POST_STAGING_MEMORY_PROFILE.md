# Asphalt 3 memory after installer staging

On `wasm-port` commit `808ba26bf`, the live `.lan` launcher served WASM
SHA-256 `8812ae4b5646ff32ca5d50d683c02aae3388dfbfbec3d0572f2ae0801534d592`.
Chromium 153 used the stock Nokia 5320 firmware and persistent OPFS device.
The uninstrumented Asphalt 3 route reached the racing HUD with sound enabled,
presented 275 frames over five seconds of unpaced gameplay, and reported no
guest errors. Its endpoint consumed **2,696.24 MiB of whole-browser PSS**.
The matching Tetris marathon-board route consumed **2,634.84 MiB** after 15
seconds. Prepared Hooked On consumed **2,294.37 MiB** at the tutorial-dismissed
fishing scene on a persistent device; a transient-device repeat consumed
2,333.35 MiB. Asphalt 3 remains the largest *retained gameplay endpoint* of
the three previous leaders on this release; the other catalogue games were not
rerun. Hooked On's first persistent launch hit a PlayServer timeout before
gameplay, then its repeat completed the scene without guest errors. A separate
same-binary Asphalt 3 run with browser-native profilers enabled reached
the same HUD at 2,664.27 MiB PSS. Its 32 MiB difference shows run-to-run and
profiler variation, not a measured allocation saving.

## Production browser RAM

This table is from the **uninstrumented** Asphalt 3 endpoint. Linux `smaps`
PSS measures the browser processes' share of resident CPU pages. The WASM
memory had 1,448.75 MiB of virtual capacity, of which 623.42 MiB was resident
PSS. The reconciliation is the difference between sequential mapping reads and
process `smaps_rollup` reads, balanced at the displayed precision. PSS omits
unmapped disk cache and external GPU VRAM; it is not a native phone's RAM use.

| # | Mapping owner | PSS, MiB | What it includes |
|---:|---|---:|---|
| 1 | Chromium PartitionAlloc | 969.09 | Browser-native allocations, including V8 module data; sampled call sites below are a separate view. |
| 2 | WASM linear memory | 623.42 | Emulator C++, guest memory, mapped ROM and WasmFS. |
| 3 | V8 executable code and trampolines | 462.78 | Mostly compiled guest ARM translations. |
| 4 | File-backed mappings | 429.14 | Chromium libraries and mapped files, not just game assets. |
| 5 | Other V8 sandbox mappings | 116.72 | Browser-owned mappings. |
| 6 | Other V8 mappings | 53.34 | Browser-owned mappings. |
| 7 | Other anonymous or shared mappings | 40.69 | Stacks and miscellaneous browser mappings. |
| 8 | Sequential-sampling reconciliation | 1.06 | `smaps_rollup` total less the mapping reads, rounded to make displayed rows sum. |
| 9 | **Total browser PSS** | **2,696.24** | Sum of rows 1–8. |

The second production-binary run enabled V8 perf-map and native allocation
sampling. Its executable mapping was 457.36 MiB PSS; 109,354 present 4 KiB
pages (**427.16 MiB**) intersect generated guest-function labels. The perf map
is append-only and can retain labels after code-address reuse, so this is not
an exact per-module live-code partition. Guest `asphalt3.exe` began at ARM
`0x74e00000`; that 1 MiB guest-code slot had 8,518 logged generated functions
and 47.62 MiB of cumulative native *code lengths*. Those lengths are neither
the slot's current resident bytes nor an extra amount to add to browser PSS.

Native lifetime samples estimated **328.34 MiB** still allocated at V8's
`GetAndCopyFirstArgumentAsBytes`, **108.53 MiB** at `WasmCode` construction,
and **70.44 MiB** at trap-handler registration. The generated-module handoff
at [`aot_runtime.cpp`](../../emu/cpu/src/aot/aot_runtime.cpp) line 652 passes a
`Uint8Array` view to `new WebAssembly.Module`; the JavaScript side does not
make another explicit copy, but V8 samples show its native module-input copy.
Sampling cannot assign those allocations byte-for-byte to the PartitionAlloc
or executable rows, so they must **not** be added to the table. Prior
[module-lifetime testing](MODULE_LIFETIME_RESULTS.md) showed that superseded
modules lose their table references and V8 can reclaim executable residency
after GC. The current 140,000-plus compiled functions make generated-code
volume a stronger hypothesis than an established unreachable-module leak.

## C++ and guest origins inside WASM

An isolated build of the same source enabled C++ allocation stacks and guest
memory events; its WASM SHA-256 is
`b06f251497c4d86b4eec033335c44ddd33ba982d58ed71ad332960243183b11a`.
It had no compatible derived ROM-code cache and includes profiler storage, so
its **661.449 MiB** WASM mapping is a *different diagnostic run*, not a
breakdown of the production 623.42 MiB row. It reached the same racing HUD
without guest errors. The allocator recorded 34,202,326 allocations with zero
dropped records, unknown frees or truncated stacks. Linux pagemap assigned
whole resident pages only to live allocations of at least 64 KiB; the
unassigned row keeps all other pages in the measured mapping rather than
pretending they have a known owner.

| # | Diagnostic WASM owner | Resident PSS, MiB | Allocation path or limit |
|---:|---|---:|---|
| 1 | Per-process direct arenas | 181.973 | [`process.cpp`](../../emu/mem/src/model/multiple/process.cpp) lines 52 and 73 reserve a stable 48 MiB per process and clear grown prefixes. |
| 2 | Guest chunk backing outside arenas | 79.363 | [`chunk.cpp`](../../emu/mem/src/model/multiple/chunk.cpp) line 403 reserves sparse backing for Symbian chunks. |
| 3 | ROM and mapped files | 66.707 | One live ROM mapping at [`virtualmem.cpp`](../../emu/common/src/virtualmem.cpp) line 312. |
| 4 | Worker stacks and thread storage | 35.258 | Pthread startup from [`main.cpp`](../../emu/wasm/main.cpp) line 158. |
| 5 | Emulated OS services | 29.723 | Includes 19.98 MiB in five 4 MiB redraw-builder allocations reached through [`winuser.cpp`](../../emu/services/src/window/classes/winuser.cpp) line 1576, plus font data. |
| 6 | WasmFS file storage | 28.973 | Live `wasmfs::MemoryDataFile::write` buffers for ordinary files. |
| 7 | ROM and executable loading | 11.121 | Loader/process construction, including [`loader.cpp`](../../emu/services/src/loader/loader.cpp) line 126. |
| 8 | ARM compiler and code cache | 9.605 | Mostly the 8 MiB page table allocated at [`memory_experiment.cpp`](../../emu/cpu/src/aot/memory_experiment.cpp) line 13. |
| 9 | Other large allocations | 5.145 | Mixed source-mapped stacks. |
| 10 | ARM CPU state | 4.215 | Per-thread emulated CPU contexts. |
| 11 | Unassigned WASM pages | 209.366 | Includes allocations below the 64 KiB unwind threshold, profiler storage, `sbrk` FBS backing, free/allocator pages and block boundaries; their individual resident shares were not measured. |
| 12 | **Diagnostic WASM mapping PSS** | **661.449** | Sum of rows 1–11; never add to the production table. |

The paused race snapshot identifies **13 live process arenas**. Together they
reserve 624 MiB of WASM address space, publish and clear 160 MiB of prefixes,
and contain only **9.852 MiB currently committed** by direct guest chunks.
Pagemap found 160 MiB resident within prefixes and **21.973 MiB resident
beyond them**. Of that tail, 16.000 MiB is in `ngiplay0x20003b78`, 4.824 MiB
in `AknIconSrv`, and 1.145 MiB in `DownloadMgrServer`; the remaining 0.004
MiB is in `wmdrmpkserver`. The two largest tails occupy arena addresses
previously held by released WasmFS write buffers. This is old page residency,
not 22 MiB of live guest data. The analogous pre-staging lineage run measured
[170.87 MiB of tail](DIRECT_ARENA_RESIDENCY.md), so the new result is
consistent with the OPFS installer staging removing most of that pressure.
The captures are different launches and builds, and whole-browser savings
cannot be inferred from this tail difference alone.

The largest individual guest reservation is `AknIconSrv`'s 128 MiB local
chunk at guest `0x01700000`. At the paused race point it had **4 KiB guest
committed and 4 KiB resident** in its 128 MiB host backing. Its reservation
is therefore not a 128 MiB physical leak. The 96 MiB global FBS large chunk
had 2.316 MiB guest committed; its stable `sbrk` backing at
[`fbs.cpp`](../../emu/services/src/fbs/fbs.cpp) line 75 is not individually
unwound by the malloc profiler. The unresolved 209.366 MiB row includes
100.970 MiB of *live allocator payload* in sub-64-KiB allocations and 33.828
MiB of diagnostic profiler storage, but those sizes are virtual/live-byte
figures, not measured resident subdivisions of the row.

The clearest remaining retention targets are generated WASM code and its V8
module data, then the arena-prefix policy and the five redraw buffers. The
module data's avoidable share is not established. Shrinking arena prefixes
would alter the direct-memory fast path and prior fine-growth tests did not
lower whole-WASM PSS; the 160-versus-9.852 MiB gap is therefore a measured
host/guest mismatch, not a safe 150 MiB saving. The redraw allocations remain
live through the race, but this capture does not prove they are unused by later
frames. Sparse chunk maximum sizes are mostly nonresident and should not be
ranked as RAM waste.

A [later post-staging retest](DIRECT_ARENA_FINE_GROWTH_ADOPTION.md) found that
1 MiB growth steps reduced WASM PSS by 26–40 MiB in paired Asphalt 3 runs.
The 48 MiB reservations and their stable host pointers remain in place.

Raw production captures are under
`/home/claude/.scratch/eka-memory-20261017-after-staging/` in
`asphalt-3/`, `tetris/`, `asphalt-3-full/`, `hooked-on-transient/`, and
`hooked-on-persistent-repeat/`. The failed first Hooked On path is in
`hooked-on/`. The native and JIT analysis is
`asphalt-3-full/browser-memory-attribution.json`.
The source-mapped diagnostic captures are `asphalt-3-diagnostic/` and
`asphalt-3-diagnostic-race/`, including `allocation-attribution.json` and the
paused `race-arena-residency.json`. The matching Chromium debug ELF has build
ID `9edff2492605d22e9fdebd338b9713367b83286b`.
