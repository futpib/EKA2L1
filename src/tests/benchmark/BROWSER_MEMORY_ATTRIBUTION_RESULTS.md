# Whole-browser memory attribution: Asphalt 3

Asphalt 3 led the previous eight-game [memory ranking](ALLOCATION_MEMORY_PROFILE_RESULTS.md)
at 3298.8 MiB browser PSS. It also led the latest uninstrumented two-game
recheck: 3014.0 MiB in its race, versus 2943.8 MiB for Tetris. Those captured
scenes do not establish a universal maximum across every level or device. This
profile replayed Asphalt 3's actual race with guest sound enabled, verified its
position and lap HUD, rendered 383 frames in the five-second capture window,
and recorded no guest errors. The served production WASM SHA-256 was
`d31435fa2b15ab989776f661ce7afc6bfb416b953474193adf992bc1f7a2f7dd`;
Chromium was 153.0.8010.52. The browser ran with allocation samplers and
V8's perf-map flag, so its 3170.77 MiB PSS is a **diagnostic measurement**, not
a new production memory baseline or a throughput result.

## Physical RAM ledger

PSS accounts for each resident shared page fractionally. The 3170.77 MiB
process total is the sum of all Chrome processes in the capture:

| # | Process owner | PSS MiB |
| ---: | --- | ---: |
| 1 | Renderers, including the 2694.59 MiB game renderer | 2795.12 |
| 2 | GPU process | 185.58 |
| 3 | Browser process | 148.40 |
| 4 | Network service | 34.26 |
| 5 | Storage service | 7.41 |

Every captured process also has a full Linux `smaps` file. Summing their
physical mapping PSS assigns the entire browser to these disjoint buckets:

| # | Mapping owner | PSS MiB | What is inside |
| ---: | --- | ---: | --- |
| 1 | WASM linear-memory backing | 1186.00 | Emulator C++ heap, guest chunks, process arenas, WASMFS and other WASM data |
| 2 | Chromium PartitionAlloc | 902.58 | Browser-native allocations, including sampled V8 module inputs and code metadata |
| 3 | V8 executable mappings | 493.86 | Native code for translated ARM regions, emulator WASM, JS and trampolines |
| 4 | File-backed mappings | 405.07 | Chromium binary/libraries and other mapped files across all processes |
| 5 | Other V8 sandbox mappings | 94.98 | V8-managed memory outside the large WASM backing mapping |
| 6 | Other anonymous/shared mappings | 48.20 | Stacks, shared buffers and miscellaneous anonymous memory |
| 7 | Other V8 mappings | 38.95 | Remaining V8 anonymous memory |

The mapping total is 3169.64 MiB, 1.13 MiB below the process rollup because
the two `/proc` snapshots were taken sequentially. No process map was missing.
These buckets cover physical PSS, but a mapping name is not an exact C++
allocation site. In particular, the 1908.7 MiB WASM capacity reported by this
run is reserved address space; only 1186.0 MiB of its backing was resident.

## Generated-code pages

The capture copied V8's perf map and read Linux pagemap for executable `[anon:v8]`
regions while the renderer was alive. The map contains 90,259 unique native
code entries named `f_`, `r_` or `s_`, matching the ARM translators' and AOT
runtime's generated exports. Its logged code bytes are **not** physical RAM:
the perf map is append-only and addresses can be reused. Classifying only
present executable pages gives:

| # | Present V8 executable page label | MiB |
| ---: | --- | ---: |
| 1 | Intersects only generated ARM-code entries | 426.42 |
| 2 | Intersects only other logged code entries | 11.12 |
| 3 | Has no matching perf-map entry | 56.44 |

There were no mixed-label pages. The 493.98 MiB pagemap page total differs
from the 493.86 MiB `smaps` PSS by 0.12 MiB due to sequential reads. This is
strong evidence that generated guest code dominates V8's executable memory,
but a stale entry could label a page whose code has since been replaced.
Unlabelled pages and that reuse caveat prevent claiming exact live byte ownership.

## Allocation origins inside those mappings

A matching Chromium debug ELF symbolized the renderer's native allocation
samples. These are sample-weighted estimates of allocation origins, **not**
additional resident bytes, and must not be added to the PSS ledger:

| # | Native allocation origin | Sample estimate MiB |
| ---: | --- | ---: |
| 1 | V8 `GetAndCopyFirstArgumentAsBytes`, during `WebAssembly.Module` creation | 327.97 |
| 2 | V8 `WasmCode::WasmCode` metadata | 110.14 |
| 3 | V8 trap-handler registration data | 70.86 |
| 4 | Blink `StringImpl::CreateUninitialized` | 54.59 |
| 5 | V8 streaming decoder finish | 10.61 |

The AOT runtime first copies module bytes into a JavaScript `Uint8Array`, then
passes them to `new WebAssembly.Module`; V8's sampled native copy is a separate
allocation on that path. The Blink string bucket includes DevTools/profiler
traffic, so it should not be assigned wholesale to normal game play. The
native sampler estimated 690.08 MiB in total; it does not partition every
PartitionAlloc page or identify every still-live allocation.

The separate V8 JavaScript-heap sampler estimated 83.83 MiB across 34 isolates.
The active game worker contributed 52.14 MiB, including 34.05 MiB at
`js_instantiate_aot_module`. This is another sampled view within browser memory,
not another amount to add to the PSS or native-sample totals.

The source-instrumented C++ capture in
[the compiled-only memory report](COMPILED_ONLY_MEMORY_PROFILE.md) attributes
large allocations **within** the WASM linear-memory bucket: 349.0 MiB resident
guest chunk backing, 308.0 MiB resident process-arena pages, and 200.6 MiB
resident WASMFS storage. That capture used a different instrumented WASM build,
so its C++ figures are explanatory and cannot be reconciled numerically with
this run's 1186.0 MiB. Its 832 MiB arena allocation size must not be mistaken
for 832 MiB resident RAM.

## Repeatable capture and limits

Run `arena-growth-benchmark.mjs GAME NEW_OUTPUT SERVER_URL full-memory`, then
`summarize_browser_memory.py NEW_OUTPUT --chromium-debug MATCHING_DEBUG_ELF`.
The summarizer rejects a mismatched Chromium build ID and emits
`browser-memory-attribution.json`. The capture records every browser process's
PSS and `smaps`, renderer native and JS samples, generated-code labels, and
resident executable-page classification. Keep raw captures outside Git.

Raw runs and derived JSON are under
`/home/claude/.scratch/eka-full-memory-20261015/asphalt3-jit-residency/`.
Two earlier independent Asphalt 3 native captures yielded about 330 MiB of
module-input copies, 107–111 MiB of `WasmCode` allocations, and 70–71 MiB of
trap-handler data, consistent with this run. A separate optional Chrome
memory-infra trace was attempted twice, but detailed and light explicit dumps
both returned `success: false` for the game renderer; it did not provide an
allocator tree. That option remains opt-in. Exact per-site ownership of every
browser-native byte is therefore unresolved; the physical PSS ledger itself
is complete to the 1.13 MiB snapshot skew.
