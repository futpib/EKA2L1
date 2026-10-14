# Allocation and resident memory ownership

On 2026-10-14, **Asphalt 3 has the largest measured gameplay footprint through
the current LAN launch paths: 3298.8 MiB (3.22 GiB) browser PSS**. Tetris is close
at 3233.2 MiB; prepared Hooked On is 3218.0 MiB. This is an observed ranking in
the captured scenes, not a guarantee about every level or device.

The earlier largest capture, Hooked On at 3901.7 MiB, used a fresh N-Gage import.
The current LAN service supplies its prepared installation. Repeating that
actual launch path produces 3213.0–3218.0 MiB. Installation state therefore
matters to both C++ and browser compilation memory. Persistent user disks and
longer gameplay can produce different footprints.

## Ranking and scope

All sizes below are MiB, using PSS summed across that isolated browser's
processes. The top three were remeasured against the actual production LAN
artifact. The five smaller games reuse the previous turn's captures of the
same production artifact; they were not remeasured here. Routes are unpaced
diagnostic gameplay, with guest audio enabled and browser output muted.

| # | Game | Browser PSS | C++ allocated | Launch measurement |
| ---: | --- | ---: | ---: | --- |
| 1 | Asphalt 3 | 3298.8 | 1822.8 | Fresh installation, racing HUD |
| 2 | Tetris | 3233.2 | 1821.2 | Fresh installation, marathon board |
| 3 | Hooked On | 3218.0 | 1740.7 | Prepared installation, boat scene, help dismissed |
| 4 | K-Rally | 2224.0 | 1127.9 | Previous capture, tutorial driving |
| 5 | Asphalt 2 | 2178.6 | 1124.3 | Previous capture, San Francisco barrier |
| 6 | Super Miners | 2121.8 | 1122.3 | Previous capture, challenge level 1 |
| 7 | Snakes | 2111.8 | 1121.4 | Previous capture, opening level |
| 8 | Sky Force | 2069.3 | 1119.4 | Previous capture, stage 1 |

N-Gage itself is a client rather than a game; its previous idle capture was
2663.8 MiB browser PSS. This task profiles the largest game, Asphalt 3.

## How allocation ownership was measured

Chrome's profiler cannot observe C++ suballocations within an existing WASM
memory object, as established by the [controlled Chrome probe](CHROME_MEMORY_PROFILER_RESULTS.md).
A private build therefore replaces the C allocator entry points and wraps the
Emscripten builtin aliases, including the builtin path used by mmap and worker
creation. A thread-local recursion guard avoids double-counting libc's internal
allocations. Failed realloc retains the old ownership. Small allocations use
allocator size metadata for lifetime accounting; only blocks at least 64 KiB
need a pointer registry and JS/WASM allocation stacks.

The instrumented Asphalt 3 run reaches the racing HUD with no guest/page errors.
It observes **31,028,522 successful allocation events**, with zero lost records,
zero unknown frees and zero truncated stacks. At every captured stage, usable
bytes plus four dlmalloc header bytes per live block exactly reconcile with
`mallinfo().uordblks`. The final usable total is 1820.85 MiB; this establishes
allocator coverage rather than treating browser samples as C++ allocation data.
Call stacks cover **94.92%** of those live bytes. The remaining 92.58 MiB consists
of smaller allocations whose individual stacks are deliberately not unwound.

WASM offsets in allocation stacks are decoded with the same module/source-map
parser used by native CPU attribution. Function ranges are checked against the
exact WASM artifact. Unmapped source positions remain unmapped. Inlining can
hide the allocating C++ function name: the 64 MiB arena calls show
`multiple_mem_model_chunk::do_create`, but their source anchor is
`mem/src/model/multiple/process.cpp:53`. The nearest allocating project frame
distinguishes these from callers elsewhere in `process.cpp`.

For physical ownership, a live 64-byte marker verifies the native address of
the WASM backing mapping through `/proc/PID/mem`. `/proc/PID/pagemap` then counts
present pages wholly inside each tracked large allocation. This avoids guessing
which anonymous mapping represents WASM memory. It excludes boundary pages,
does not expose PFNs and does not add overlapping allocator blocks. The guest
is not globally frozen, so these are page residency observations, not an atomic
snapshot across all threads. Small-block and static-storage residency remain
unattributed. This Linux method was exercised successfully, not merely proposed.

## Asphalt 3 C++ owners

These are final diagnostic allocation values, not production PSS. The private
build has 33.08 MiB of static diagnostic storage; its physical contribution was
not measured separately. The original production LAN artifact remains deployed.

| # | Owner | Live usable allocation, MiB | Resident whole pages, MiB |
| ---: | --- | ---: | ---: |
| 1 | Process arenas | 832.00 | 200.06 |
| 2 | Guest chunk backing outside arenas | 348.00 | 348.00 |
| 3 | WASMFS file storage | 262.05 | 199.62 |
| 4 | ARM interpreter state and related buffers | 129.30 | 129.24 |
| 5 | Allocations below 64 KiB | 92.58 | Unattributed |
| 6 | ROM and mapped files | 66.71 | 66.71 |
| 7 | Worker stacks and thread storage | 40.02 | 37.95 |
| 8 | Emulated OS service buffers | 25.80 | 25.73 |
| 9 | ROM and executable loading buffers | 12.24 | 12.09 |
| 10 | ARM compiler and code cache | 10.86 | 10.83 |
| 11 | Other large allocations | 1.28 | 1.25 |

The 13 arenas reserve 64 MiB each. Initialized prefixes total 192 MiB; measured
resident pages total 200.06 MiB. Residency can exceed initialized prefixes when
the allocator reuses previously touched pages. The other roughly 632 MiB of
arena reservation should not be described as resident RAM. It still occupies
the bounded WASM address space. Allocation and physical figures must not be
added together.

The guest chunk row includes a **96 MiB FbsLargeChunk**, whose allocation stack
leads to `fbs_server::initialize_server`; all 96 MiB is resident. Canonical chunk
mapping calls `common::map_memory(max_size_)`, and the WASM anonymous mmap path
allocates and clears the complete reservation. Thus reservation maxima become
real resident buffers in this path. Other chunk allocations include 130 MiB
from generic guest chunk creation and 48 MiB of code-segment chunk backing;
the allocation stacks identify their subsystems without inventing object names.

WASMFS's largest group is 256.20 MiB across 345 live `MemoryDataFile::write`
vector allocations, with 193.86 MiB of resident whole pages. It includes the
firmware, installed files and staging filesystem; this capture does not assign
each vector to an individual filename or establish how much is discardable.

## A concrete unused allocation

The largest individual interpreter allocation is **125.032 MiB**, of which
125.027 MiB is resident. The allocating call is inlined into
`ensure_system_started`; its source position points to libc++ `make_unique`.
The exact WASM ABI layout independently establishes `sizeof(ARMul_State)` as
131,105,704 bytes, matching this allocation's requested size. The only matching
live allocation is identified, and its `trans_cache_buf_top` is read at the
compiler-derived field offset.

`TRANS_CACHE_SIZE` is **125 MiB**, while the captured decoded instruction use
is **4,754,008 bytes (4.53 MiB)**. The used position rises from 4.25 MiB after
loading to 4.53 MiB after the gameplay window. Approximately **120.47 MiB of
buffer capacity is unused at this capture**. Sources are
`cpu/include/cpu/dyncom/armstate.h:35,404` and
`cpu/src/dyncom/arm_dyncom.cpp:36`.

This is a concrete candidate for lazy allocation or bounded growth. The profile
does not establish an adequate maximum for every workload. Returning an already
touched block to malloc also does not guarantee an immediate PSS reduction:
WASM linear memory does not shrink, and freed pages can remain resident.
Avoiding this upfront reservation could reduce heap pressure and later growth;
the resulting physical savings and throughput would need their own experiment.

## Browser memory outside C++ allocation accounting

At the final diagnostic snapshot, browser PSS is 3350.84 MiB. The main renderer
accounts for 2859.09 MiB, including **1223.05 MiB** in the marker-verified WASM
backing mapping. Its other mappings include 1307.63 MiB of other anonymous
pages, 279.13 MiB of anonymous executable pages, and 49.28 MiB of file-backed
pages. The other browser/GPU/service processes account for about 492 MiB.
The executable row includes JIT code and trampolines; it does not identify how
much belongs to generated ARM regions versus the runtime or JavaScript.

Chrome sampling attaches 34 isolates with no profiler errors. Retained sampled
V8 allocations total **84.29 MiB**; the largest site is about **42.9 MiB** at
`js_instantiate_aot_module`. Native memory sampling estimates **1000 MiB**.
Neither number is an exact partition of PSS, and neither should be added to
the allocator or backing-store totals. Other browser-native anonymous memory
remains unresolved by allocation site. This report attributes the C++ heap
and the resident pages of its large allocations without claiming complete
browser-native ownership.

The tracked application allocation peak is 1859.23 MiB. This does not include
unobserved temporary buffers inside a single libc allocator operation. Browser
PSS is sampled once per second and at route checkpoints; the largest observed
value is 3350.84 MiB, which is not a bound on subsecond peaks. Diagnostic FPS
is excluded from performance conclusions.

## Evidence and verification

The compact checked-in [JSON](ALLOCATION_MEMORY_PROFILE_RESULTS.json) contains
the baseline inputs, artifact hashes, snapshots, mapped stacks, residency data
and Chrome summary. Full profiles, screenshots, browser logs, smaps, allocator
controls and build outputs remain in
`/home/claude/.scratch/eka-allocation-profile-20261014/`.

Production WASM SHA-256:
`a196638dd3770b4dbebee36b1139f616589a24fd4bdd41fac60b97f597302cf8`.
Diagnostic WASM SHA-256:
`baa1e0ef75a59c62d76ccb70d657f280bbe246b5a3ed8156814ecb08fd1853b9`.
Its C++ map SHA-256:
`a0d95c353dc6c1d2cdeda815f0c2b8d461e175d20a118b7f5e024f1e1aaf4938`.

The browser allocator control verifies allocation/free/mmap, growing and
shrinking realloc, failed realloc, 122,889 successful allocations with shuffled
small frees, and malloc/free called through the JS exports. This caught and
fixed libc-internal double accounting before the accepted captures. An earlier
Hooked On diagnostic capture using that incomplete hook is discarded. The
first Asphalt 3 baseline stopped at the Instant Play menu; its failure is kept,
and the route now recognizes that menu before measuring. The successful
baseline and both accepted diagnostic captures show the actual racing HUD.

Temporary diagnostic servers are stopped after collection. The allocation
build option defaults off; no normal launch switches or allocator hooks are
added to production execution.
