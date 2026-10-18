# Asphalt 3 launch allocation attribution (2026-10-18)

Source commit: `73f32705e` on `wasm-port`. An isolated allocator-instrumented
build (`eka2l1.wasm` SHA-256 `58cc6e0e7d12d6ff4a74fab6dc2758d22285de45c5ce3229219ee94cf0c14f22`)
served a fresh Chromium Asphalt 3 launch on port 39571. It had no compatible
precompiled ROM cache, so compiler timing and browser allocation amounts are
diagnostic-build observations, not an exact production comparison. The `.lan`
build was unchanged. Snapshots bracketed `eka2l1_run` and were taken again
about 300 ms after it returned. The allocator reported no dropped records or
unknown frees and reconciled exactly with `mallinfo` at all three snapshots.

| # | Live C++ allocation owner and source | Before to `run` return | Return to +300 ms | Total |
|---:|---|---:|---:|---:|
| 1 | Process direct arena, `multiple_mem_model_process::direct_local_backing` in `src/emu/mem/src/model/multiple/process.cpp:52` | 48.000 MiB | 0 | 48.000 MiB |
| 2 | FBS/OS services, mostly `font_store::add_fonts` from `fbs_server::initialize_server` in `src/emu/services/src/fbs/impls/font_store.cpp:40` and `font.cpp:1160` | 0.963 MiB | 8.332 MiB | 9.295 MiB |
| 3 | ARM compiler and direct-memory page table, chiefly `memory_experiment::view::enter` in `src/emu/cpu/src/aot/memory_experiment.cpp:13` | 0 | 8.956 MiB | 8.956 MiB |
| 4 | Allocations below the profiler's 64 KiB stack threshold | 1.075 MiB | 2.808 MiB | 3.884 MiB |
| 5 | Guest chunk backing, `multiple_mem_model_chunk::do_create` in `src/emu/mem/src/model/multiple/chunk.cpp:416` | 1.125 MiB | 2.000 MiB | 3.125 MiB |
| 6 | Emulator pthread stack, `pthread_create` in `src/emu/wasm/main.cpp:945` | 2.001 MiB | 0 | 2.001 MiB |
| 7 | ARM CPU state | 0 | 0.375 MiB | 0.375 MiB |
| 8 | Other large allocations | 0.165 MiB | 0 | 0.165 MiB |
| | **Total live allocation delta** | **53.330 MiB** | **22.471 MiB** | **75.801 MiB** |

`applist_server::launch_app` calls `kernel_system::spawn_new_process`, which
ultimately reserves the 48 MiB contiguous process arena. Its guest-visible
initialized prefix was only 3 MiB at `run` return and 4 MiB at +300 ms.
Marker-verified `/proc/pagemap` found 6 MiB of resident whole pages inside
this allocation at +300 ms; the rest is reserved WASM address capacity, not
48 MiB of resident RAM.

The FBS path reads three stock ROM fonts through OPFS: `heiseigoths60.ttf`
(2,903,683 bytes), `s60sc.ccc` (2,344,855), and `s60tchk.ccc` (2,043,263).
The generated WasmFS `__wasmfs_opfs_read_blob` uses `blob.slice(...).arrayBuffer()`;
its 7.29 MB of large read requests agree with Chromium's sampled
`blink::FileReaderAccumulator` allocation site (~8.31 MiB estimated). The
font adapters retain copied font data and associated structures in the WASM
heap. An additional 2 MiB guest chunk backing allocation belongs to FBS.

The direct-memory page table is one million 8-byte entries: `pages_.resize(1<<20)`
allocates 8 MiB on the first CPU run. Remaining compiler allocation includes
translated Thumb regions and generated WASM module buffers. The browser's
native sampler separately estimates ~8.14 MiB at V8's
`GetAndCopyFirstArgumentAsBytes`, reached from
`new WebAssembly.Module(wasmBytes)` in `src/emu/cpu/src/aot/aot_runtime.cpp:655`.
That is a V8-owned module-byte copy; it is not included in the C++ allocation
table. The executable mapping's PSS also grows as V8 compiles the module.

| # | Memory measure | Before `run` | `run` return | +300 ms |
|---:|---|---:|---:|---:|
| 1 | C++ live allocations | 142.04 MiB | 195.37 MiB | 217.84 MiB |
| 2 | WASM linear-memory capacity | 307.25 MiB | 368.75 MiB | 442.50 MiB |
| 3 | Main renderer PSS | 627.14 MiB | 617.41 MiB | 661.98 MiB |
| 4 | Whole-browser PSS | 1211.34 MiB | 1202.79 MiB | 1248.72 MiB |
| 5 | WASM backing mapping PSS | 282 MiB | 288 MiB | 288 MiB |

The ~37.38 MiB whole-browser PSS rise from before `run` to +300 ms is **not**
the 75.80 MiB C++ live allocation delta. The main renderer's PSS rose
34.84 MiB; its mapping changes include +6 MiB WASM backing, ~10.67 MiB
Chromium PartitionAlloc, ~7.92 MiB executable V8 mappings, and ~6.88 MiB
other V8 sandbox mappings. The native allocation profiler's site amounts are
sampled estimates, not additional exact PSS, and cannot be summed with these
mapping figures. A separate launch had the same 48 MiB arena allocation but
only a ~16 MiB whole-browser PSS rise over the corresponding interval, showing
that browser residency and reclamation timing are material.

The largest exact avoidable-looking allocation is the full 8 MiB guest page
table; its access and alias semantics must be preserved before changing it.
The 48 MiB arena dominates allocator *reservation*, but only 6 MiB was resident
at this snapshot. Font buffers and V8 module creation are the other concrete
launch owners, with different lifetimes and memory layers.

Raw diagnostic evidence is under
`/home/claude/.scratch/eka-launch-allocation-capture-2/` (allocator snapshots,
PSS smaps, native samples, pagemap residency) and
`/home/claude/.scratch/eka-launch-allocation-capture-5/opfs-reads.json`
(font filenames and read sizes).
