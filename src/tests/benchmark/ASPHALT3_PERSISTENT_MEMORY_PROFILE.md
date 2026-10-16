# Asphalt 3 persistent-device memory profile (2026-10-16)

Asphalt 3 was the largest browser-memory user in the prior supported-game
comparison. This capture uses the current persistent-device route, including
read-only OPFS firmware offload, and reaches the racing HUD with no guest
errors. A clean production build and an allocation-instrumented diagnostic
build were measured separately; diagnostic FPS is not a throughput result.

The production capture is in
`/home/claude/.scratch/eka-asphalt3-persistent-browser-20261016` (WASM SHA-256
`b3b320d8927b5994933db650c29d56483f1922711ff8b45b8811c1261d0cb0c7`).
The allocation capture is in
`/home/claude/.scratch/eka-asphalt3-persistent-allocation-20261016` (WASM
SHA-256 `9248556f8319d06a39ba6fa0229c561581b50a36640914ab48d760a182e387ce`).
Both used Chrome 153.0.8010.52 in private local servers, not the `.lan`
release. The allocation capture recorded 33,829,750 allocation events with no
dropped records, unknown frees, or truncated stacks. The profiler reports
1,612.56 MiB live, within 2.27 MiB of `mallinfo`'s 1,614.82 MiB. The
production capture recorded 3,122.61 MiB whole-browser PSS, 2,601.27 MiB
renderer PSS, and 1,086.38 MiB resident WASM linear-memory backing. Its
1,910.56 MiB WASM capacity is not resident memory.
The guest chunk commitment trace is a third, otherwise equivalent run in
`/home/claude/.scratch/eka-asphalt3-chunk-highwater-2-20261016`; it reached
the racing HUD for 385 frames without errors. Its only runtime changes were
temporary creation/adjustment log lines, which were removed from source after
the capture.

| # | Production browser mapping | PSS, MiB | Attribution limit |
|---:|---|---:|---|
| 1 | WASM linear-memory backing | 1,086.38 | C++ owner detail comes from the separate diagnostic run below. |
| 2 | Chromium PartitionAlloc mappings | 967.18 | Native sampling gives allocation sites, not a complete live-owner partition. |
| 3 | V8 executable code and trampolines | 441.97 | 397.85 MiB of present pages intersect guest-region labels in the V8 perf map. |
| 4 | File-backed mappings | 424.79 | Includes Chromium and other mapped files across processes. |
| 5 | Other V8 sandbox mappings | 112.66 | Not attributed to a specific emulator object. |
| 6 | Other V8 mappings | 47.58 | Not attributed to a specific emulator object. |
| 7 | Other anonymous/shared mappings | 40.90 | Not attributed to a specific emulator object. |

The mapped PSS categories are from `/proc/*/smaps`. Their sum differs from
the separately read `smaps_rollup` browser total by about 1.15 MiB because
the processes continued running between reads. V8's native allocation sample
names `GetAndCopyFirstArgumentAsBytes` at 342.9 MiB sampled estimate,
which matches module creation in `src/emu/cpu/src/aot/aot_runtime.cpp:654-657`;
this sample is **not** a measurement of retained bytes. There are 140,262
compiled guest functions at the end of the production run, 127,471 from
demand compilation, and 5,585 retired. The perf map is append-only and can
contain stale addresses; it establishes that guest-generated code occupies
about 398 MiB of resident executable pages, but does not precisely split
those resident pages by Symbian DLL. Its logged code labels include EUser,
EDbms, Avkon, Tagma and EStor. The large code footprint is measurable; the
avoidable portion is not established by this profile.

The biggest ROM module by cumulative perf-map code labels is EUser.dll
(about 71.5 MiB across Liftoff and TurboFan entries). These are generated
machine-code lengths, **not** a live-RSS figure or an ARM execution-time
ranking. The exact ROM bytes at two labelled entries are ordinary ARM memory
primitives, for example:

```asm
EUser.dll 0x801A0460  cmp  r1, #8
EUser.dll 0x801A0470  orr  r2, r2, r2, lsl #8
EUser.dll 0x801A0474  orr  r2, r2, r2, lsl #16  ; byte-fill pattern
EUser.dll 0x801A07AC  ands ip, r1, #3
EUser.dll 0x801A07CC  ldrhs lr, [r1], #4    ; aligned-copy path
```

These labels show which guest code produced native code. They do not imply
that these ARM instructions caused the 128 MiB Symbian chunk allocation; the
separate `chunk_new` trace below does establish that call path.

| # | Diagnostic C++ owner | Live, MiB | Whole-page resident, MiB | Main source |
|---:|---|---:|---:|---|
| 1 | Process arenas | 832.00 | 261.62 | `src/emu/mem/src/model/multiple/process.cpp:53` |
| 2 | Guest chunk backing | 349.01 | 349.00 | `src/emu/mem/src/model/multiple/chunk.cpp:380` |
| 3 | WASMFS file storage | 160.05 | 97.57 | `wasmfs::MemoryDataFile::write` |
| 4 | Allocations below the 64 KiB unwind threshold | 101.90 | Unknown | The profiler counts these but does not capture their C++ stacks. |
| 5 | ROM/mapped-file copy | 66.71 | 66.71 | `src/emu/common/src/virtualmem.cpp:307` |
| 6 | Worker stacks and thread storage | 42.03 | 39.95 | `src/emu/wasm/main.cpp:158` and pthread startup |
| 7 | Emulated OS services | 29.80 | 29.73 | Includes five 4 MiB graphics blocks at `src/emu/services/src/window/classes/winuser.cpp:1562`. |
| 8 | ROM/executable loading | 11.22 | 11.10 | Loader and process construction. |
| 9 | ARM compiler/code cache | 9.58 | 9.55 | Includes an 8 MiB flat page table at `src/emu/cpu/src/aot/memory_experiment.cpp:13`. |
| 10 | Other large allocations | 6.02 | 5.96 | Mixed ownership. |
| 11 | ARM CPU state | 4.26 | 4.21 | Thread CPU contexts. |

The process arenas reserve 13 fixed 64 MiB address ranges. Their 832 MiB
live allocator size is **not** 832 MiB physical RAM: only 261.62 MiB of their
whole pages were resident, and the runtime reported 192 MiB initialized arena
capacity at the diagnostic endpoint. The direct mapping path needs stable
host pointers (`process.cpp:49-71`), so this reservation should not be called
a leak. The per-chunk `memset(max_size_)` at `multiple/chunk.cpp:378` can still
touch uncommitted guest bytes inside those arenas; this capture does not
separate that share from useful arena contents.

The strongest avoidable retention is much more specific:

| # | Object and origin | Backing resident | Guest committed at the racing HUD | Evidence |
|---:|---|---:|---:|---|
| 1 | `AknIconSrv` local chunk, max 128 MiB | 128.00 MiB | 0.004 MiB | Guest `chunk_new` trace, then all adjustments through the racing-HUD run. |
| 2 | Host FBS `FbsLargeChunk`, max 96 MiB | 96.00 MiB | 2.316 MiB | `fbs_server::initialize_server()` and all adjustments through the same route. |
| 3 | WASMFS ROM file buffer | 66.707 MiB within a 128 MiB vector | ROM file is already mapped separately | Allocation stack, page residency, and exact first-256-byte match to `SYM.ROM`. |

For rows 1 and 2, the emulator maps each maximum-size backing in
`multiple_mem_model_chunk::do_create()` at `multiple/chunk.cpp:370-381`, even
though `committed_` starts at zero (`multiple/chunk.cpp:366`). On this browser,
those two allocations have every whole page resident. The guest uses only
4 KiB of the 128 MiB local chunk and 2.316 MiB of the 96 MiB FBS chunk by the
racing-HUD endpoint: **221.68 MiB resident above guest commitment**. This is
a measured discrepancy, not a prediction of how many bytes a replacement
allocator would immediately return to Chrome.

The 128 MiB chunk is a real Symbian request, not a guess from a C++ stack. A
temporary trace of `src/emu/kernel/src/svc.cpp:1451-1488` recorded process
`AknIconSrv`, `initial_bottom=0`, `initial_top=0`, `max_size=0x08000000`,
guest PC `0x8019DF64`, and LR `0x801A85B7`. The exact expanded ROM
(`SYM.ROM` SHA-256
`89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490`)
disassembles around that handoff as:

```asm
EUser.dll 0x801A85B2  blx  0x8019DF60   ; Thumb wrapper calls SVC stub
EUser.dll 0x8019DF60  svc  #0x6A         ; ARM mode, chunk_new on this firmware
EUser.dll 0x8019DF64  bx   lr            ; PC observed by the host bridge
```

The matching bridge registration is `src/emu/kernel/src/svc.cpp:6458`.
The trace identifies the process and EUser call boundary, but did not unwind
the caller inside `AknIconSrv.exe`; a precise AknIconSrv instruction address
is not established. The 96 MiB FBS chunk has a different origin: host C++
creates it in `src/emu/services/src/fbs/fbs.cpp:362-375` when the FBS server
initializes. There is no single ARM allocation instruction for that object.

The ROM is copied twice in the same process. A 66.71 MiB `map_file()`
allocation is fully resident. Separately, a WASMFS `MemoryDataFile::write`
allocation has 128 MiB capacity and exactly 66.707 MiB resident; the first
256 bytes of **both** allocations match the local `SYM.ROM`, while neither
matches `SYM.RPKG` (recorded in
`/home/claude/.scratch/eka-asphalt3-ready-fingerprint-20261016/ready-fingerprints.json`).
Read-only offload had already moved 142,102,132 firmware
bytes to OPFS. The 128 MiB vector remained live from language selection
through the race. `src/emu/common/src/virtualmem.cpp:288-311` opens the ROM
file and calls `mmap`, then returns without closing its file descriptor.
`src/emu/kernel/src/kernel.cpp:1378` is the ROM-map caller. A leaked open
WASMFS handle is the source-supported explanation for why unlinking the old
in-memory ROM file during `src/emu/wasm/persistent-disk.js:106-142` does not
release its buffer. Closing the descriptor after a successful map is the
specific hypothesis to test; this profile did not run that A/B change.

The live `/data` and `/tmp` filesystem inventory contained 9,504 regular
files totaling 67.33 MiB after offload. That logical size is distinct from
WASMFS vector capacity, mapped ROM bytes, and stale open-file objects.

The next two unproven candidates are the five 4 MiB graphics allocations
(20 MiB total, stable across captured stages) and the 8 MiB flat translation
page table. The former stack ends in retained window drawing at
`winuser.cpp:1562`, but this profile does not show whether their capacities
are used; the latter is deliberate direct-memory lookup state. Neither is
counted as reclaimable here. The two large chunks plus the duplicate ROM
account for about **288.4 MiB** of measured resident over-retention, subject
to an implementation A/B before claiming a browser-PSS saving.
