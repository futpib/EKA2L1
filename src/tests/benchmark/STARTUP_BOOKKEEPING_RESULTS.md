# Hooked On startup bookkeeping

## Scope and measurement

Hooked On has the longest observed automatic loading stage among the ten
launcher games: a fresh N-Gage installation takes about 135–137 seconds on this
host. Its first client frame appears after about 10 seconds, long before the
game can be started. Optimize that installation before moving to other games.

Measurements use the normal paced launcher in Chromium 153.0.8010.52 on an Intel
i7-10875H with NVIDIA T1000 hardware ANGLE/Vulkan. Every run has a fresh browser,
HTTP cache and guest filesystem. The server's ROM/game download cache is warm;
internet/IPFS retrieval is excluded. Only one measured emulator runs at a time.
Builds and test suites do not run during accepted timing windows. Background
host work is not disabled; reports retain load averages.

First presentation is detected through `eka2l1_presentations`. N-Gage completion
is the browser receipt time of the log batch containing the installer's
successful exit, checked against screenshots showing **Installation complete**.
Deletion of the input `.n-gage` package occurs much earlier and is not used as
completion. This is installation readiness, not the first gameplay frame:
language selection, menus and game-specific startup remain afterward.

The initial ten-game screen produced:

| # | Game | First presentation, seconds | N-Gage installation complete, seconds |
| ---: | --- | ---: | ---: |
| 1 | Hooked On | 9.89 | 135.29 |
| 2 | Asphalt 3 | 9.91 | 80.20 |
| 3 | Tetris | 9.88 | 62.19 |
| 4 | K-Rally | 9.22 | — |
| 5 | Snakes | 8.37 | — |
| 6 | Super Miners | 8.12 | — |
| 7 | Asphalt 2 | 8.03 | — |
| 8 | Sky Force | 7.79 | — |
| 9 | Pathway to Glory | 5.87 | — |
| 10 | Explode Arena | 5.14 | — |

The remaining games were screened through their initial visible output, not
through every menu to gameplay. First presentations can be logos and must not
be interpreted as equivalent game-ready measurements across games.

## Attribution and changes

A separate CPU-worker sampling capture of the original build covers 141.01
sampled seconds after the first frame. Exclusive samples include 40.22 seconds
in `InterpreterMainLoop`, 17.94 in the memory-view callback, 17.68 in
`invalidate_ram_code`, and 13.70 in page enumeration. Another 18.65 seconds are
in `emscripten_futex_wait`; these parked samples are not CPU consumption.
Invalidation accounts for roughly 20–25% of samples in busy middle-installation
windows. The outer-loop total is not an exact attribution of its inlined work.

Two sources of work were removed:

1. **Code invalidation:** every page removal scanned all current translations
   and their inlined-code dependencies, even for ordinary data pages. A page
   index now selects candidate versions. Exact byte-range overlap checks still
   decide invalidation, including dependencies and every address space. Dead
   versions cannot invalidate replacement versions. Old index records remain
   until the cache resets, matching the existing version-storage lifetime.
2. **Memory-view rebuilding:** every address-space or mapping-generation change
   cleared a million-entry, 8 MiB table before repopulating it. It now clears
   only entries published by the previous view, then rebuilds from the same
   mapping enumeration. Initial allocation remains zero-initialized. Alias,
   permission, address-space and callback behavior are preserved.

Guest timing, watchdog policy, compilation scheduling, emitted ARM translations,
mapping checks and the amount of eager compilation are unchanged. No normal
launch experiment switches are added. These changes reduce runtime bookkeeping;
they do not move compilation into gameplay or replace the installation with a
snapshot.

## Results

The initial baseline and its later repeat agree within two seconds:

| # | Build/run | Installation complete, seconds | Renderer CPU seconds after first frame | Busiest renderer thread CPU seconds |
| ---: | --- | ---: | ---: | ---: |
| 1 | Original, initial screen | 135.29 | not recorded | not recorded |
| 2 | Page index only | 113.05 | 123.75 | 91.10 |
| 3 | Page index and sparse clear | 101.25 | 109.74 | 77.75 |
| 4 | Original, later control | 137.28 | 150.08 | 114.36 |
| 5 | Combined candidate, final public `.lan` launch | 105.46 | 114.29 | 80.52 |

The combined candidate averages 103.35 seconds versus 136.29 for the controls:
about **33 seconds less waiting, a 24% reduction**. The initial sparse-clear run
removes another 11.8 seconds relative to the index-only candidate. These are small sequential
samples, not confidence intervals. CPU intervals include the post-completion
polling margin, so they are corroborating resource measurements rather than
exact installer CPU totals.

The later control and combined candidate finish with 98,907 and 98,955 compiled
functions respectively, and each records about 14.8–14.9 seconds of translation,
emission and module installation work. Small scheduling-dependent discovery
differences remain. The compiler policy and preparation work were not reduced.

Matched unpaced gameplay checks use the same routes as the recent
[Qt/WASM comparison](QT_VS_WASM_UNPACED_FPS.md), with a fresh installation in
each browser. Clock policy, assets, compiler settings, viewport and audio
configuration match between builds. No profiler is attached. The timed windows
run continuously; instruction totals remain zero. The following lists every
control and combined-candidate result, in run order within each build:

| # | Game/scene | Original FPS | Combined candidate FPS | Window |
| ---: | --- | --- | --- | --- |
| 1 | Hooked On, Costa Rica boat and introductory help | 256.76 | 285.09 | 5 s |
| 2 | Snakes, opening level, score 200 | 97.41, 99.96, 97.99 | 72.20, 99.37, 99.12 | 5 s |
| 3 | Sky Force, stage 1 | 295.82, 321.43, 220.52 | 272.36, 319.60, 328.08 | 2 s |
| 4 | Asphalt 2, Golf/San Francisco barrier | 104.57 | 135.43 | 10 s |

The first Snakes result triggered a deployment hold and alternating repeats.
The slowdown did not recur in the next two candidate runs, which matched the
controls around 99 FPS. An additional page-index-only Snakes run measured
85.38 FPS. These observations are retained; the low runs' cause was not
established. Sky Force also varies strongly on the original build. Thus this
campaign finds no consistent gameplay regression but does not establish a tight
upper bound on rare slowdowns or a precise general gameplay speedup. Do not
average away the outliers or interpret one favorable pair as a universal gain.

Screenshots confirm the intended gameplay scenes, including the boat's help
transition and the car at the barrier. Runs report no browser/guest errors.
The other six games received the initial startup screen, not matched runtime
retests in this campaign. These measurements do not replace paced playability,
full-game completion or audible-output testing. All raw values, compilation
deltas and evidence paths are in [the JSON results](STARTUP_BOOKKEEPING_RESULTS.json).

## Correctness and reproducibility

Adopt both changes. The final rebuilt WASM is byte-identical to the combined
candidate measured above. The full WASM suite passes **188 tests**, including
the restored memory suite; native CTest passes **3/3** targets. `git diff --check`
and the startup harness syntax check pass.

The native cache regression covers exact boundaries, cross-page primary and
dependency spans, address-space collisions, repeated dependencies, stale
versions, move/reset, and the top of the guest address space.

The direct-memory WASM suite checks generated ARM/Thumb accesses, aliases,
process switches, permission changes, memory and syscall callbacks, page
removal and observer lifetime. Its standalone callback fixture initially
failed in both the old and new memory implementation: it selected the RAM
path which intentionally exits before a missing-memory callback. The fixture
now selects the callback-capable path it intends to test. This suite is also
included in the ordinary full test run, where it was previously omitted.

Run a fresh normal-launch measurement:

```sh
node src/tests/wasm/startup.mjs https://claude-laptop.lan:8188/ NEW_OUTPUT hooked-on 240
```

Inspect the saved screenshots and logs before accepting a result. The harness
does not press menu buttons or measure gameplay. Its CPU interval starts after
the first presentation and ends after observing completion, including polling
and screenshot margins. Linux thread CPU comes from scheduler counters;
renderer process totals also include compilation, rendering and audio.

Raw campaign evidence is under
`/home/claude/.scratch/eka-startup-20261012/`: `baseline-*/`,
`control-repeat-hooked-on/`, `page-index-hooked-on/`,
`sparse-clear-hooked-on/`, `profile-workers-hooked-on/`, and `runtime-*/`.
The initial profiling attempt `profile-hooked-on/` discovered no CPU worker
and is rejected for attribution. The successful capture enabled broad CDP
target discovery before navigation and started worker profilers concurrently;
parked workers must not block attaching to the executing worker. Diagnostic
profile timings are excluded from the startup comparisons.

Build identity (source base `2283f58b9` on `wasm-port`, plus this change):

| # | Build | WASM SHA-256 |
| ---: | --- | --- |
| 1 | Original production control | `ad02135c672a175de7fb48e17b10df17cfdfc70385278002996599b31ff13284` |
| 2 | Page index only | `2c61077c21358203052971d770fed5e4c2b1def514d07c1a08b14e29afdfa208` |
| 3 | Page index and sparse clear | `b61ef34a52f800493da8a9d952c8f6ed65bd27e6f38b2a7f093c34c43f1fd7a7` |

The public HTTPS endpoint serves the combined candidate from the frozen
`sparse-clear-build/` directory, and its downloaded WASM matches the hash above.
The final run through the committed startup harness reached installation
completion in 105.46 seconds, with the expected screenshot, active normal
compiler/watchdog settings and no browser or guest errors. Evidence is in
`public-repeat-hooked-on/`.
The original served build remains available for rollback. Preview ports were
only used to compare immutable artifacts against the unchanged production
control; no game/firmware binaries are committed.
