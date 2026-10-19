# Generated WASM module lifetime

Cold-region merging replaces each old export in Emscripten's `wasmTable` and
`wasmTableMirror`. The temporary `WebAssembly.Module` and
`WebAssembly.Instance` variables then leave scope. Emscripten's
`functionsInTableMap` is a `WeakMap`; it does not own the old exports.

I measured the default browser build with stock Tetris in Chromium 153. An
isolated diagnostic copy of the generated JS counted replaced table slots and
paused the emulator worker immediately before and after calling the worker's
exposed `gc()` at the 512th cold merge. `/proc` PSS was sampled during each
pause. The diagnostic code and forced GC were never added to the production
build.

| # | Observation at the 512th merge | Value |
| ---: | --- | ---: |
| 1 | Generated modules created | 12,018 |
| 2 | Old modules with zero remaining table slots | 11,144 |
| 3 | Table slots replaced | 34,894 |
| 4 | Input WASM bytes of zero-slot modules | 103,393,079 bytes |

| # | PSS, KiB | Before worker GC | After worker GC | Change |
| ---: | --- | ---: | ---: | ---: |
| 1 | Game renderer, all mappings | 1,295,414 | 1,271,352 | -24,062 |
| 2 | Game renderer, executable V8 mappings | 155,276 | 136,748 | -18,528 |
| 3 | Game renderer, PartitionAlloc mappings | 373,899 | 368,303 | -5,596 |
| 4 | All Chromium processes | 1,799,240 | 1,783,308 | -15,932 |

The 103 MB figure is source-module byte length, not resident memory. The GC
interval also collects unrelated objects; the PSS deltas cannot all be
assigned to old generated modules. Another Chromium process grew by about
8 MiB during the interval, explaining why the total browser drop is smaller
than the renderer drop. The direct evidence is that V8 executable pages do
leave residency after the old table entries are replaced and GC runs.

`src/tests/wasm/module-lifetime-fixture.mjs` reproduces the table, mirror and
`WeakMap` reference pattern with 2,000 distinct modules. After replacing all
2,000 exports and requesting three browser GCs, **zero** old module or
instance `WeakRef`s were reachable. Its aggregate Chromium PSS fell from
415,045 to 392,731 KiB; executable V8 PSS fell from 8,240 to 208 KiB. This
establishes collection for the reference pattern; the Tetris run establishes
resident-memory reclamation in the emulator. Run the fixture with:

```sh
cd src/tests/wasm
node module-lifetime-fixture.mjs /tmp/module-lifetime.json 2000
```

The audit also found a separate cache-retention path. A superseded or failed
RAM-code validation marked its version dead without queuing it for
`collect_retired()`, leaving its table function and snapshot payload installed
until a later explicit memory invalidation. These paths now call `retire()`.
Process retirement also queues all remaining RAM and single-region functions
from that address space. Table entries are removed only at the next CPU
boundary, after generated frames return; shared functions remain installed
until their last owner retires.

## Smaller generated modules in fresh game routes

The normal CPU build flushes initial generated modules at 16 demand-compiled
or 32 eagerly compiled functions and merges cold modules in 64-function
batches. An isolated candidate reduced those limits to 4, 8, and 16. This
changes both initial and merged module sizes, so the experiment does not
attribute any result to one of the three limits individually.

Both builds came from the same source and WASM build configuration, apart from
those three constants. Their WASM SHA-256 values were `539758eae827091c`
for the control and `5840a0d78d8ce996` for the candidate. Chromium
154.0.8037.97 used fresh profiles, fresh N-Gage installation, guest audio,
no prepared game image or ROM translation cache, a 4,096 MiB all-renderer
cgroup, and zero swap. Runs were sequential: control/candidate/candidate/
control for Asphalt 3, then control/candidate for Tetris. All reached their
asserted gameplay scene with no OOM events. The high cap leaves the peaks
uncensored; it is not a claim of passing the 1,536 MiB target.

| # | Game | Build | All-renderer peak, MiB | Whole-browser peak PSS, MiB | Unpaced FPS | Final CPU Worker's AOT installation, s |
|---:|---|---|---:|---:|---:|---:|
| 1 | Asphalt 3 | Control 1 | 1472.7 | 1935.3 | 113.1 | 3.09 |
| 2 | Asphalt 3 | Smaller 1 | 1500.3 | 1965.2 | 114.7 | 3.99 |
| 3 | Asphalt 3 | Smaller 2 | 1481.2 | 1981.8 | 125.6 | 4.32 |
| 4 | Asphalt 3 | Control 2 | 1499.2 | 1933.9 | 127.2 | 3.49 |
| 5 | Tetris | Control | 1451.0 | 1913.7 | 158.6 | 3.23 |
| 6 | Tetris | Smaller | 1440.4 | 1877.9 | 144.0 | 3.55 |

The AOT timer resets on CPU-worker recycling; its entries describe only the
final worker epoch, not total route compilation time. Asphalt's cgroup peaks
overlap across builds, so there is no repeatable
all-renderer memory win. Both smaller-module Asphalt runs have higher
whole-browser peak PSS and higher AOT installation time than either control;
these are correlated observations, not a byte-level attribution to module
objects. The single Tetris pair is too small and host-clock-dependent to
establish a memory or FPS change. The earlier one-run 32-function cold-merge
experiment also gave no useful all-renderer headroom; see
`RENDERER_CAP_RESULTS.md`.

V8 can reclaim an unreachable module, as the earlier replacement/GC evidence
above shows, but each additional module has construction and metadata costs.
These route runs measure net residency, not the number of V8 modules collected.
The tested smaller batches did not produce a useful net result on these routes.
The candidate was rejected; normal batch sizes and the deployed build remain
unchanged. Raw results and build snapshots are under
`/home/claude/.scratch/eka-small-modules-{control,candidate}*`.
