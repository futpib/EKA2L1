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
