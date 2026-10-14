# Compiled RAM code reclamation

Checked 2026-10-13 on `wasm-port`, Chromium 153.0.8010.52, i7-10875H,
hardware ANGLE/Vulkan and the stock Nokia 5320. The retained change reclaims
translations after their guest mappings are invalidated. It has no new launch
switch, and adds no checks to generated ARM-in-WASM instructions.

## Does code actually unload?

Two fresh normal paced Hooked On launches were investigated: ordinary
installation and the prepared filesystem image. Both reached Costa Rica boat
gameplay, accepted movement and ran a ten-second presentation window without
captured guest/page errors. Temporary probes recorded executable chunk
destruction and RAM-cache invalidation; shutdown was timestamped separately.

| # | Path before explicit shutdown | Destroyed code chunks | Newly invalidated RAM regions |
| ---: | --- | ---: | ---: |
| 1 | Prepared image, through gameplay | 0 | 8 |
| 2 | Ordinary installer, through gameplay | 2 | 945 |

The installer destroys two 1 MiB reservations at completion, around 48.67 host
seconds in this run. Their actual compiled spans cover fewer pages than their
maximum reservations. More than a thousand probe messages at explicit shutdown
are excluded. Most named code chunks stay loaded during this short gameplay
route. This is not an exhaustive claim about every game or a long session.

Mapping invalidation also finds obsolete translations that the old cache had
already marked dead. Thus the retained implementation releases about 35,800
function references in the longer unpaced route, substantially more than the
945 newly invalidated regions in the paced installer observation. These counts
measure different lifetimes; they are not evidence of 35,800 library unloads.

## Retained implementation

Unmap/invalidation callbacks mark matching translations unloaded and queue
their version IDs, including superseded versions of those mappings. The CPU
collects the queue before entering its next generated run. It releases copied
instruction/dependency buffers, removes unloaded entries from affected page
indexes, clears their WASM function-table slots and lets the SDK reuse slots.
Late staged compilations for unloaded versions are discarded too.

Compilation IDs remain stable tombstones and are never reused. Live dispatch
entries and mapping checks remain intact. Immutable ROM translations stay
resident. Ordinary lookup failures and replacement of still-mapped code retain
their existing behavior; they do not initiate reclamation themselves.

The existing cumulative compiled-function counter is preserved. An atomic
retired-function count is added to the existing monitor report; updates occur
only when a batch of functions is released. There is no guest-instruction
accounting, per-instruction instrumentation or collection within a generated
region.

## Performance and memory

Fresh-browser runs are serial and unprofiled, use identical input routes and
firmware/game bytes, and keep guest audio enabled with browser output muted.
Hooked On uses the ordinary installer path, completes the anchor introduction
and dismisses tutorial help. Three consecutive approximately twenty-second
unpaced windows run without pauses inside each window and stay below the same
1,800-second guest deadline. Start/end screenshots were checked. The route
contains initial compilation and timer stepping before measurement.

| # | Variant | Unpaced FPS, pooled over its three windows |
| ---: | --- | ---: |
| 1 | Baseline A | 240.16 |
| 2 | Unload-only A | 236.22 |
| 3 | Baseline B | 243.93 |
| 4 | Unload-only B | 253.93 |
| 5 | Baseline C | 243.73 |

Across all accepted baseline windows the pooled result is **242.61 FPS**;
unload-only windows give **245.07 FPS**, a 1.02% difference. Individual windows
vary substantially in both controls and candidates, so this is not an
established speedup. No consistent runtime slowdown was observed in these
repeats. The short Snakes opening-level control gives **101.10 -> 101.15 FPS**,
with matching scenes and no captured guest/page errors. It is a five-second
smoke comparison, not a precision throughput campaign.

Whole-browser PSS was sampled after each FPS window, outside the measured
interval, in unload-only B and baseline C. It sums proportional resident memory
for the Chromium process tree, avoiding RSS double-counting of shared mappings.
No forced garbage collection or CPU profiler was used.

| # | Mean of three endpoint samples | Baseline C | Unload-only B | Difference |
| ---: | --- | ---: | ---: | ---: |
| 1 | Browser-tree PSS | 4,617.50 MiB | 4,602.97 MiB | -14.53 MiB |
| 2 | Emulator allocator bytes | 1,865.33 MiB | 1,860.94 MiB | -4.39 MiB |

This memory pair is a small observed reduction, about 0.31% of browser PSS,
not an all-client guarantee. It does not solve the much larger fixed arena and
interpreter-cache allocations. The WASM memory capacity does not shrink.

The baseline runtime SHA-256 is
`9670e79c417c64f51f129b2b99a907f8a8c112571b4bd98af811d4bab06dc022`;
the retained runtime is
`85101f86d5c48b6e6eb5df2e2e90114c3de67f86fbb52df45d7058be61bfe109`.
Firmware, client SIS and game-container identities are unchanged.

## Public browser verification

The retained build is deployed at `https://claude-laptop.lan:8188/`. A download
from that endpoint matches the retained WASM SHA-256 above. A fresh paced
Chromium launch through the normal automatic launcher restored the existing
prepared Hooked On image, reached the Costa Rica boat and accepted movement.
The ten-second gameplay window presented **31.87 FPS** at **1.00015x guest
time**, with zero instruction accounting and no captured guest/page errors.
The final monitor recorded nine retired function references. Start/end images
show gameplay and movement; the existing touch-control overlay remains visible.
This verifies the public prepared-image path separately from the ordinary
installer used for the unpaced comparisons.

## Validation and rejected attempts

Native CPU tests pass **915 assertions in 37 cases**. The WASM suite reports
**189 passed, zero failed**, retaining its separately labeled pre-existing
crash-reproduction XFAIL. New tests check deferred removal, removal of obsolete
dependency indexes, rejection of late attachment, stable compilation IDs,
actual WASM table clearing, slot reuse and execution after remapping.

A broader prototype also retired ordinary cache replacements and lookup
failures immediately. It averaged 215.31 FPS in one three-window run versus
240.16 for its control and was discarded. Those noisy observations do not
establish its precise cause; the retained version restricts retirement to
invalidated mappings. Its source patch and reports remain outside Git.
An initial native fixture incorrectly checked dependencies without providing
a resolver and was corrected. An early private-server request raced listener
startup; that failed harness attempt is excluded and preserved.

Raw probe builds, prototype patches, routes, screenshots, build/test logs,
unload timestamps and FPS/memory reports:
`/home/claude/.scratch/eka-aot-unload-20261013`.
`unload-observations.json` separates shutdown from active execution;
`benchmark-summary.json` records the accepted windows and discarded variant.
