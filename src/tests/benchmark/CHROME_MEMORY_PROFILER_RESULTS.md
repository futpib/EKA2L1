# Chrome memory profiling with WASM source maps

Tested 2026-10-14 on Chromium 153.0.8010.52 with the graduated runtime from
`8be6adbd8`. Chrome's allocation tools are useful for JavaScript/V8 objects and
browser-managed native allocations. **They cannot replace our allocator and
arena measurements for C++/guest allocations inside WASM linear memory.**
Source maps cannot attribute allocation events the profiler never records.

This conclusion is based on live controls, an actual Snakes startup/gameplay
capture, and the same WASM module/source-map decoder used for native CPU
attribution. It is not inferred solely from profiler documentation.

## Controlled shared-WASM probe

The fixture uses Emscripten's real allocator, optimization, WASM function names,
C++ source maps and pthread/shared memory, with an initial 128 MiB linear memory.
Its map is served over HTTP and checked byte-for-byte against the build output.
All controls hold their objects alive until sampling stops. Both CDP
`HeapProfiler.startSampling` and `Memory.startSampling` use 16 KiB intervals;
native sampling is started once per renderer, not once per worker.

| # | Operation | C++ allocator increase | V8 heap samples | Native estimated allocation | WASM capacity |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Allocate and fill 32 MiB through WASM `malloc` | 32 MiB + 8 bytes | 0 bytes | 48 KiB, 3 small unrelated samples | 128 → 128 MiB |
| 2 | Retain a 16 MiB JavaScript double array | 0 bytes | 16 MiB + 8 bytes | 48 KiB, 3 small browser samples | 128 → 128 MiB |
| 3 | Request the same JavaScript allocation from C++ | 0 bytes | 16 MiB + 8 bytes | 16 KiB, 1 small unrelated sample | 128 → 128 MiB |

The large C++ allocation is verified by a nonzero returned pointer, filling the
allocation, and `mallinfo().uordblks` increasing by 33,554,440 bytes. It fits
inside existing linear memory. Both Chrome profilers miss the actual allocation;
the native records are small Chrome allocations, not a 32 MiB WASM allocation.
The two JavaScript controls establish that allocation sampling is working.

A heap snapshot records `system / JSArrayBufferData` at **134,217,728 bytes**:
the complete backing store. It does not divide that store into C++ allocations
or describe the ownership of the held 32 MiB block. A snapshot's backing size is
also not a physical-residency measurement.

The C++-to-JavaScript control provides one WASM frame, named `js-to-wasm:i:i` at
WASM offset 1869. The existing module parser resolves that offset to
`allocate_js_from_cpp`. The source-map decoder finds an explicit unmapped
anchor at offset 1868. This is an entry wrapper, not the allocating instruction;
we report **no allocation-site line**, rather than borrowing a nearby C++ line.
Repeating with observable work after the JS call, preventing a tail-call-only
fixture, produces the same limitation. Both original and repeat captures remain
in the evidence directory.

## Actual emulator capture

Snakes runs through normal installation, startup, eager compilation and the
opening level on the real LAN HTTPS service. Sampling begins before navigation;
new worker isolates are attached as they appear. The capture has **34 isolates**
and zero profiler/guest/page errors. The memory profiler changes execution cost;
its FPS field is diagnostic and is not used for a throughput comparison.

The sum of retained V8 sampled allocations across isolates is **50.49 MiB**.
The native renderer profiler returns **5,651 samples**, estimating **387.27 MiB**.
Neither quantity is total browser RAM, and they must not be added to C++ heap,
WASM capacity or PSS counters. Worker captures stop sequentially; their endpoints
are not one simultaneous global heap snapshot.

The C++ allocator reports **1120.30 MiB** at the route's last sample, WASM
capacity is **1194 MiB**, and initialized arena prefixes total **64 MiB**.
The allocator figure includes reservations, including untouched arena tails;
it is not resident memory. The controlled probe demonstrates why Chrome's
allocation profiles cannot break this C++ heap down by allocation site.

The largest V8 sampled worker has **19.80 MiB**, including **13.98 MiB** attributed
to `js_instantiate_aot_module`. Other visible sites include `updateTableMap`,
typed-array `set`, `wasmTable.get`, and main-page log joining. This is useful
JavaScript ownership/churn evidence, not a ranking of C++ or guest allocations.

All **24 WASM frames** in the heap profiles are entry wrappers. Module metadata
resolves identities such as `eka2l1_init`, device/SIS installation, graphics
preparation and thread entry points. None provides a sampled C++ allocation-site
position. Native heap stacks contain Chrome addresses rather than WASM byte
offsets; our WASM source maps cannot directly decode them. The native CPU adapter
could contextualize some JIT addresses, but it cannot recover the unrecorded
in-buffer C++ allocations established by the control.

## What is viable

Use Chrome heap profiles for JavaScript allocations and retained objects,
including AOT module glue. Use native memory profiles for browser allocator
investigations with their own symbol/coverage limits. Keep OS PSS for physical
residency, WASM capacity for the memory object's size, and allocator/arena
counters for emulated backing ownership. Guest address-access distributions
still require guest-aware diagnostics.

Source-mapped C++ allocation ownership would need an allocation-event source
inside the WASM allocator, such as diagnostic sampled allocation hooks or a
dedicated allocator walker with ownership metadata. No production allocator
hooks or per-memory-access instrumentation were added by this investigation.

Chrome documents allocation sampling as a JavaScript-function profiler in
[its memory guide](https://developer.chrome.com/docs/devtools/memory-problems#investigate-memory-allocation-by-function).
The installed DevTools protocol also explicitly distinguishes native `Memory`
sampling from V8 `HeapProfiler` sampling; its descriptions and method support
were checked against the live browser. The protocol definitions are available
in [the official repository](https://github.com/ChromeDevTools/devtools-protocol/tree/master/json).

## Reproduction and evidence

```sh
em++ src/tests/wasm/memory-profiler-fixture.cpp -O2 -gsource-map \
  --profiling-funcs -pthread -sPTHREAD_POOL_SIZE=0 \
  -sINITIAL_MEMORY=134217728 -sMAXIMUM_MEMORY=2147483648 \
  -sALLOW_MEMORY_GROWTH=1 -o /tmp/memory-fixture/memory-profiler-fixture.html
node src/tests/wasm/chrome-memory-probe.mjs /tmp/memory-fixture /tmp/memory-probe
node src/tests/wasm/arena-growth-benchmark.mjs snakes /tmp/game-memory HTTPS_URL chrome-memory
python3 src/tests/benchmark/summarize_chrome_memory.py /tmp/game-memory \
  --wasm BUILD/eka2l1.wasm --map BUILD/eka2l1.wasm.map --program eka2l1.wasm
```

Compact results and exact hashes are in
[CHROME_MEMORY_PROFILER_RESULTS.json](CHROME_MEMORY_PROFILER_RESULTS.json).
Raw `.heapprofile` files open in DevTools, and the fixture `.heapsnapshot`
opens in the Memory panel. Raw native-memory profiles, map/entry joins, fixture
WASM/maps, logs and actual game screenshots remain under
`/home/claude/.scratch/eka-arena-graduation-20261014`.
