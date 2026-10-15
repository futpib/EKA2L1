# Browser memory after compiled-only graduation

Measured on 2026-10-15 against the public `wasm-port` launcher. Its served
WASM SHA-256 was
`d31435fa2b15ab989776f661ce7afc6bfb416b953474193adf992bc1f7a2f7dd`.
The routes reached a Tetris marathon board and an Asphalt 3 race with guest
sound enabled and no guest errors. Each used a fresh browser and guest disk;
Asphalt 3 loaded its build-matched ROM translation cache. These are unpaced
diagnostic scenes, not a forecast of memory use on a phone.

| # | Scene | Browser PSS MiB | Largest renderer PSS MiB | WASM capacity MiB | Compiled guest functions |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Tetris marathon board | 2943.8 | 2549.0 | 1886.6 | 145801 |
| 2 | Asphalt 3 race | 3014.0 | 2621.5 | 1908.4 | 140293 |

The 1 Hz observed Asphalt 3 peak was 3057.5 MiB. A separate Chrome-sampled
Asphalt 3 run ended at 3098.9 MiB; profiler overhead and different execution
make it a diagnostic sample, not another production baseline. The two games
are close enough that this does not establish a universal largest game.

## Where the production renderer's 2621.5 MiB resides

The Linux `smaps` capture gives a physical-memory view. Its largest mapping is
the 1908.4 MiB V8 sandbox backing for WASM linear memory, of which 1176.6 MiB
was resident. Capacity is not physical RAM use.

| # | Renderer mapping | PSS MiB | Attribution |
| ---: | --- | ---: | --- |
| 1 | Large writable WASM backing mapping | 1176.6 | Guest/emulator C++ heap, filesystem buffers and other WASM data |
| 2 | Chromium `partition_alloc` mappings | 769.1 | Browser-native allocations; sampled stacks do not reliably identify their owners |
| 3 | V8 anonymous executable mappings | 487.6 | JIT code and trampolines; generated guest WASM contributes, but its exact share is unknown |
| 4 | Other renderer mappings | 188.2 | Additional V8 mappings, Chromium image, shared buffers and minor mappings |

The remaining browser, GPU and utility processes account for about 392.5 MiB
of the 3014.0 MiB total. The 487.6 MiB executable figure is **not** the C++
code cache: generated WASM is compiled into V8-owned native code outside the
linear-memory allocator. `smaps` cannot distinguish our translated ARM code
from the emulator's WASM runtime, JavaScript JIT and trampolines within those
executable mappings.

## C++ allocation sites and resident pages

An isolated source-mapped build enabled allocator-lifetime instrumentation and
replayed the same Asphalt 3 race. It used a different WASM binary
(`c6962134587dbf4d9700f8a26f421896dcb44bcebcf67aad3613471e9a9df8a4`)
and compiled the ROM on demand, so its 3132.5 MiB browser PSS must not be
substituted for the production 3014.0 MiB. It observed 33,289,835 allocation
events, with zero dropped records, unknown frees or truncated stacks; live
usable bytes reconcile exactly with `mallinfo` plus allocator headers. A marker
verified the WASM backing mapping before Linux pagemap counted resident whole
pages inside large live allocations. The profiler itself has 33.08 MiB of
static storage. Allocations below 64 KiB lack individual stack attribution.

| # | Owner and allocating code | Live allocation MiB | Resident whole pages MiB |
| ---: | --- | ---: | ---: |
| 1 | Per-process arenas, [`process.cpp`](../../emu/mem/src/model/multiple/process.cpp) | 832.00 | 308.00 |
| 2 | Guest chunk backing, [`chunk.cpp`](../../emu/kernel/src/chunk.cpp) and [`virtualmem.cpp`](../../emu/common/src/virtualmem.cpp) | 349.01 | 349.00 |
| 3 | WASMFS `MemoryDataFile::write`, Emscripten SDK | 262.05 | 200.62 |
| 4 | Blocks smaller than 64 KiB | 100.48 | Not attributed |
| 5 | ROM and mapped files | 66.71 | 66.71 |
| 6 | Worker stacks and thread storage | 40.02 | 37.95 |
| 7 | Emulated OS services | 29.80 | 29.72 |
| 8 | ROM/executable loading buffers | 11.22 | 11.10 |
| 9 | C++ ARM compiler and code cache | 9.58 | 9.56 |
| 10 | ARM CPU state | 4.26 | 4.21 |
| 11 | Other large allocations | 2.40 | 2.35 |

The arenas reserve 13 × 64 MiB, while their initialized prefixes total
192 MiB and their measured resident pages total 308 MiB. Previously touched
pages can remain resident after reuse; the other reserved bytes are not
necessarily RAM. Guest chunk backing is different: this path allocates and
clears the whole requested range. Its largest item is the 96 MiB
`FbsLargeChunk` created by [`fbs_server::initialize_server`](../../emu/services/src/fbs/fbs.cpp),
all resident. Other groups include 130 MiB in three guest chunks and 49 and
34 MiB in code-segment-related chunks. The 256.2 MiB WASMFS vector group
accounts for 194.9 MiB resident; these stacks identify `MemoryDataFile::write`
but not which files can be released safely.

The source mapper historically labelled any `ARMul_State` allocation as
"interpreter state". That fallback is now labelled "ARM CPU state"; the actual
independent interpreter-state check retains its separate label. The compiled-only
capture's CPU state is 4.26 MiB live, and the former 125 MiB decoded-instruction
buffer is absent. This does not remove the guest chunk, filesystem or V8 code
costs.

## JavaScript and browser code ownership

Chrome attached to all 34 isolates of the public build without profiler errors.
Its retained V8 allocation samples total 84.48 MiB. The active worker contributes
53.14 MiB, including about 32.25 MiB at
[`js_instantiate_aot_module`](../../emu/cpu/src/aot/aot_runtime.cpp), 8.57 MiB at
`addFunction`, and 3.23 MiB at `wasmTable.get`. These are sampled JavaScript
allocations associated with installing generated modules, not a partition of
the 487.6 MiB native executable mapping. Chrome's native sampler estimated
675.17 MiB but provided no WASM-labelled stacks or symbols sufficient for
source attribution. All 24 source-mapped WASM heap frames were JS-to-WASM
entry wrappers, not C++ allocation sites. These sampled quantities must not be
added to PSS or to the allocator table.

This points to two distinct large opportunities: guest chunk backing with
specific owners such as the 96 MiB FBS allocation, and browser-native code/data
from roughly 140,000 compiled guest functions. The former has measured C++
allocation sites and resident pages; the latter has a measured executable
mapping and JS installation sites, but no exact per-guest-function resident
breakdown. A 9.58 MiB C++ cache estimate alone would badly understate the
memory cost of generated code.

Raw production captures, screenshots, `smaps`, Chrome profiles and diagnostic
allocation stacks are under
`/home/claude/.scratch/eka-compiled-only-20261015/`. In particular,
`memory-production-asphalt3/instant-play-stationary-sound-on.json`,
`memory-production-asphalt3/instant-play-stationary-sound-on-3847076.smaps`,
`memory-diagnostic-asphalt3/allocation-attribution.json` and
`memory-production-chrome-asphalt3/chrome-memory-summary.json` hold the figures
above. The public build stayed unchanged. A relink after turning diagnostic
instrumentation off produced a different SHA because the frozen public binary
has an extra `eka2l1_guest_failure_trace` export absent from current source;
its exact bitwise source reproducibility remains unresolved.
