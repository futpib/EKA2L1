# Asphalt 3 memory after sparse chunk backing

This is a fresh profile of the largest measured game scene on the current
browser launcher. Source commit `d7cfeab96` and the deployed default WASM
`8bacf4e12fd1eb11f61a04a2f54dbfbbcdcfca056b7a305132d58337b115e006`
run stock Nokia 5320 firmware and reach Asphalt 3's race HUD with sound enabled
and no guest errors. The deployed derived ROM cache is bound to that exact WASM
hash. These are isolated Chromium 153 desktop browser PSS measurements, not
phone RAM predictions or universal maxima across every game level.

The latest known top three were rechecked after sparse chunk backing. The
released, cached configuration makes **Asphalt 3 the measured leader**; without
its cache, Asphalt 3 and Tetris are within 2.3 MiB of each other. All three
rows below used a fresh browser, the normal persistent-device path, completed
firmware offload, the same WASM binary and an actual gameplay HUD. The other
five supported games were substantially smaller in the earlier catalogue
sweep, but were not rerun on this binary.

| # | Scene and code cache | Browser PSS, MiB | Observed peak, MiB | V8 executable PSS, MiB |
|---:|---|---:|---:|---:|
| 1 | Asphalt 3 race, cache loaded | 2972.65 | 3037.09 | 500.56 |
| 2 | Asphalt 3 race, cache absent | 2904.78 | 2962.39 | 432.50 |
| 3 | Tetris marathon board | 2902.55 | 2943.13 | 434.57 |

The cached and uncached Asphalt runs had nearly identical compiled-function
totals at the endpoint (140,284 and 140,232), but the cache moved roughly
70,000 functions from demand compilation into preload. In this single pair,
the cache added 67.87 MiB browser PSS, including 68.06 MiB more executable
pages, and reached the race HUD at 73.21 rather than 77.79 seconds from page
navigation. That timing is a one-pair observation, not a stable startup
speedup. The code-memory difference is real for these captured scenes; the
precise reason V8 retains more native code despite similar function counts
remains unproved. The cache remains deployed for its startup benefit.

## Physical browser ownership

The uninstrumented cached race's `smaps` files assign physical PSS across
every Chromium process. Their sum is 1.2 MiB below the process rollups because
the active processes were sampled sequentially. WASM *capacity* was 1709.75
MiB; that is reserved address space, not resident RAM.

| # | Mapping owner | PSS, MiB | Meaning |
|---:|---|---:|---|
| 1 | Chromium PartitionAlloc | 945.36 | Browser-native allocations, including V8 module/code metadata |
| 2 | WASM linear-memory backing | 879.44 | Emulator C++, guest chunks, process arenas, filesystem and ROM data |
| 3 | V8 executable code and trampolines | 500.56 | Generated guest code, runtime WASM, JS and trampolines |
| 4 | File-backed mappings | 443.31 | Chromium and other mapped files |
| 5 | Other V8 and anonymous mappings | 202.78 | Sandbox, heaps, stacks and shared buffers |

A separate capture on the **same deployed binary** attached Chrome's allocation
samplers after `Module.calledRun`, then loaded the cache and reached the same
race HUD. It measured 2982.52 MiB PSS, 506.27 MiB executable PSS and 34 V8
isolates without profiler errors. Its live pagemap found 110,346 present
executable pages (431.04 MiB) intersecting `f_`, `r_` or `s_` guest-translation
labels. The perf map is append-only; code-address reuse can leave stale labels,
so this is strong attribution to generated ARM translations, not exact live
ownership of each function or Symbian DLL. Logged guest PCs include
`tagma.dll` Thumb `0x80B4F09C`, `etext.dll` Thumb `0x80B3A472`, and `euser.dll`
ARM `0x801A0460`; no single region explains the whole executable bucket.

Chrome's native allocation sampler estimates 331.69 MiB at V8's
`GetAndCopyFirstArgumentAsBytes`, 109.97 MiB at `WasmCode` construction and
71.27 MiB at trap-handler registration. The first site is reached from
[`js_instantiate_aot_module`](../../emu/cpu/src/aot/aot_runtime.cpp), which
copies generated module bytes and calls `new WebAssembly.Module`. These are
sampled *allocation origins*, not retained-byte partitions of the 945.36 MiB
PartitionAlloc mapping. They must not be added to PSS. Delayed sampling omits
the runtime's initial setup but includes game loading and cache installation.

## C++ allocations and guest commitment

A private build of the same source enabled allocator lifetimes, C++ source
maps and guest memory events. It ran Asphalt 3 through the persistent-device
path without a derived cache, because a cache cannot be loaded by a different
WASM hash. Its 3249.76 MiB browser PSS and FPS are diagnostic only. It recorded
33,983,596 allocations with zero dropped records, unknown frees or truncated
stacks; live bytes plus allocator headers reconcile exactly with `mallinfo`.
Linux pagemap and a verified marker in the renderer's WASM mapping count
resident whole pages inside live large allocations. The profiler itself has
33.08 MiB of static storage. Blocks under 64 KiB have lifetime counts but no
per-block stack.

| # | C++ owner | Live allocation, MiB | Resident whole pages, MiB | Source |
|---:|---|---:|---:|---|
| 1 | Per-process direct arenas | 832.00 | 278.00 | [`process.cpp`](../../emu/mem/src/model/multiple/process.cpp) |
| 2 | Guest chunk backing outside arenas | 349.63 | 229.75 | [`chunk.cpp`](../../emu/mem/src/model/multiple/chunk.cpp) |
| 3 | Allocations below 64 KiB | 102.50 | Unattributed | Stack-unwind threshold |
| 4 | ROM and mapped files | 66.71 | 66.71 | [`virtualmem.cpp`](../../emu/common/src/virtualmem.cpp) |
| 5 | Worker stacks and thread storage | 42.03 | 39.95 | Pthread and WASM startup |
| 6 | WASMFS file storage | 32.05 | 31.81 | `wasmfs::MemoryDataFile::write` |
| 7 | Emulated OS services | 29.80 | 29.73 | Window drawing and fonts |
| 8 | ROM/executable loading | 11.22 | 11.11 | Loader and process construction |
| 9 | ARM compiler and C++ code cache | 9.68 | 9.65 | AOT runtime |
| 10 | ARM CPU state | 4.26 | 4.21 | Compiled-only CPU contexts |

The guest event report was captured while the race was paused. It recorded
9,092 creation, commit, decommit and destruction events. The two largest
non-direct chunks show why allocation size alone is misleading:

| # | Chunk and creator | Backing size, MiB | Guest committed, MiB | Host resident, MiB | Resident above commitment, MiB |
|---:|---|---:|---:|---:|---:|
| 1 | FBS `FbsLargeChunk`, [`fbs_server::initialize_server`](../../emu/services/src/fbs/fbs.cpp) | 96.00 | 2.316 | 96.00 | 93.684 |
| 2 | `AknIconSrv` local chunk, guest `chunk_new` SVC | 128.00 | 0.0039 | 10.00 | 9.996 |

FBS creates its 96 MiB chunk at `fbs.cpp:362`, and the WASM memory model obtains
its stable host backing at `chunk.cpp:385-389`. Its [`chunk_allocator`](../../emu/services/src/allocator.cpp)
expands guest commitment only when needed. All 96 MiB nevertheless remain
resident in the diagnostic capture. This is a measured host-to-guest gap,
not a demonstrated immediate 93.684 MiB production saving: a reused WASM heap
block may already have touched pages, and host FBS pointers must remain stable.
The sparse change substantially helped `AknIconSrv`: its 128 MiB reservation
has only 10 MiB resident here. A prior bridge trace on this same firmware
placed its guest request at `EUser.dll` return PC `0x8019DF64`, through SVC
`0x6A` handled by [`svc.cpp`](../../emu/kernel/src/svc.cpp). This capture
confirms the process, size and commitment but did not unwind its ARM caller.

The 13 active direct arenas reserve 832 MiB of WASM address space; **278 MiB**
of their pages are resident, and their initialized prefixes total **192 MiB**.
The active direct guest chunks reserve 137 MiB and have only **9.863 MiB**
committed. [`direct_local_backing`](../../emu/mem/src/model/multiple/process.cpp)
zeros each newly grown prefix, and `chunk.cpp:378-381` zeros a new direct
chunk's full maximum size. These exact C++ paths touch memory beyond guest
commitment. The 268 MiB resident-to-commit gap is a priority to investigate,
but not a safe reclamation estimate: direct generated loads use the arena
without a page-table lookup, and previously touched allocator pages can remain
resident. A lazy-clear change needs a proof of zero-fill, faults and host-pointer
semantics before treating those pages as disposable.

The persistent run had 305 firmware files offloaded and 9,504 ordinary guest
files totalling 67.33 MiB of logical content. WASMFS accounts for only 32.05
MiB live in this diagnostic capture; it is no longer the dominant owner.

Raw captures and source-mapped JSON are under
`/home/claude/.scratch/eka-memory-d7cfeab96-20261016/` in
`final-asphalt-3/`, `final-asphalt-3-uncached/`, `final-tetris/`,
`asphalt-3-full-memory-delayed/` and `asphalt-3-diagnostic/`.
