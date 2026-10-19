# Browser renderer memory cap (2026-10-18)

The cold, persistent-disk Asphalt 3 route previously triggered the kernel OOM
killer in both runs with a 2,048 MiB renderer-only cgroup limit and swap
disabled. The same old build completed with a 4,096 MiB limit, reaching
2,048.5 MiB in the nearby control run. The limit includes Chromium renderer
processes for this test browser, but not its browser and GPU processes.

Large RPKG firmware files now extract directly into OPFS, and the browser
prestarts four pthread workers rather than 32. The 305 firmware files at least
64 KiB total 72,154,740 bytes (68.81 MiB). They are linked into the Z drive
without first retaining their contents in WasmFS. Smaller firmware files still
use WasmFS and IndexedDB persistence. The candidate retained both guest AOT
source metadata and C++ source maps enabled, as in the old live build.

Chrome 153 on the i7-10875H host ran the same unpaced, sound-enabled Asphalt 3
route with a fresh browser profile each time. `renderer-memory-limit.mjs`
enforced `MemoryMax` and `MemorySwapMax=0`. Times are seconds from page launch;
FPS is measured only during the stationary playable race, not installation.

| # | Build and run | Cap MiB | Peak MiB | Language s | Race s | Race FPS | Result |
|---:|---|---:|---:|---:|---:|---:|---|
| 1 | Old build, first capped run | 2048 | 2048 | — | — | — | Kernel OOM |
| 2 | Old build, second capped run | 2048 | 2048 | — | — | — | Kernel OOM |
| 3 | Old build, nearby uncapped control | 4096 | 2048.5 | 62.7 | 77.8 | 81.5 | Pass |
| 4 | Direct firmware, eight workers, maps on, run 1 | 2048 | 1975.0 | 64.3 | 77.7 | 110.6 | Pass |
| 5 | Same candidate, run 2 | 2048 | 1974.3 | 62.4 | 74.6 | 88.3 | Pass |
| 6 | Direct firmware, four workers, maps on, run 1 | 2048 | 1954.2 | 60.9 | 73.3 | 118.4 | Pass |
| 7 | Same candidate, run 2 | 2048 | 1928.0 | 61.5 | 74.6 | 110.2 | Pass |
| 8 | Final `.lan` release, run 1 | 2048 | 1945.4 | 53.5 | 66.4 | 86.0 | Pass |
| 9 | Same live release, run 2 | 2048 | 1963.0 | 63.6 | 76.7 | 120.0 | Pass |

The two live-release runs finished 102.6 and 85.0 MiB below the limit. Startup
and race-screen times did not materially regress against the nearby old-build
control. CPU frequency and host load varied across runs, so the FPS differences
are not a claim of throughput improvement. The four-worker build had four
active workers and one extra worker created on demand during launch, without an
observed launch or gameplay penalty. Snakes and Tetris also completed under
the cap with the earlier eight-worker candidate. A five-worker candidate
reached the exact cap on one of two runs; lowering the firmware cutoff to
32 KiB moved more files but gave no net memory win and slightly delayed startup.
The four-worker, 64 KiB, mapped build is deployed at `.lan`.

The exact released build also completed Tetris Marathon and the opening Snakes
level under the same cap. Tetris reached its board in 66.7 s, peaked at
1,953.5 MiB and measured 136.2 FPS in the 15 s playable capture. Snakes reached
play in 23.7 s, peaked at 1,115.0 MiB and measured 78.6 FPS in the five-second
capture. The Snakes run's CPU clock was about 3.0–3.1 GHz, versus 3.6–4.2 GHz
in an earlier eight-worker run; these runs cannot isolate the cause of its
lower raw FPS. Both games used four active workers and one returned spare.

A real browser save/reload of Snakes kept all 305 OPFS firmware links, resolved
the same sample file before and after reload, and launched from the restored
device without reinstalling firmware. The raw capped results are under
`/home/claude/.scratch/eka-tab-ram-2048-target{,-r2}/`,
`eka-tab-ram-old-paired-current-r2/`, and
`eka-tab-ram-direct-firmware-maps-on{,-r2}/`,
`eka-tab-ram-pool4-maps-on-cap{1,2}/`, and
`eka-tab-ram-final-pool4-lan-cap{1,2}/`,
`eka-tab-ram-final-pool4-lan-tetris/`, and
`eka-tab-ram-final-pool4-lan-snakes/`. The released WASM SHA-256 is
`d4b9ca1f64cf9d3ac47443818be2589bfe9e7af3c4ea570bfa29662a44aec26c`;
its JavaScript SHA-256 is
`82a34ab190aeb0986b3fbabb27a47b46fb62117ba4a249c0f2c7fe9a50b4511d`.

A low-overhead startup `smaps` capture with a 4,096 MiB control limit assigned
471 MiB resident PSS to WASM backing, 681 MiB to Chromium PartitionAlloc, and
399 MiB to V8 executable mappings in the game renderer at the language screen.
Those are mapping owners, not precise allocation sites, and the diagnostic run
is separate from the capped acceptance runs. It explains why moving another
324 smaller files to OPFS did not materially change the browser peak.

Validation: the native `epoc` target built, all 197 AOT WASM checks passed, and
the browser smoke test passed using system Chromium. The live capped runs
completed with zero kernel OOM events. The local release is
`d4b9ca1f-82a34ab1`; no game or firmware blobs were added to Git.

## 1.8 GiB target and game ranking

The same released `.lan` WASM (`d4b9ca1f64cf9d3ac47443818be2589bfe9e7af3c4ea570bfa29662a44aec26c`)
was tested again with the cap lowered from 2,048 to 1,843 MiB (1.7998 GiB).
Each of the eight supported game routes ran from a fresh Chromium profile with
persistent disk, guest audio enabled, and the renderer-only cgroup. A separate
4,096 MiB run of every route established its full peak without reaching that
higher cap. Each run reached its verified gameplay scene at 4,096 MiB. The
1,843 MiB runs were then repeated independently; an OOM peak at exactly the
cap is censored, not the game's unrestricted demand.

| # | Game | 4,096 MiB control peak (MiB) | 1,843 MiB result | 1,843 MiB peak (MiB) |
|---:|---|---:|---|---:|
| 1 | Hooked On | 2066.7 | Kernel OOM before gameplay | 1843.0 |
| 2 | Tetris | 1964.6 | Kernel OOM before gameplay | 1843.0 |
| 3 | Asphalt 3 | 1938.2 | Kernel OOM before gameplay | 1843.0 |
| 4 | Asphalt 2 | 1169.8 | Pass | 1165.5 |
| 5 | K-Rally | 1037.5 | Pass | 1075.6 |
| 6 | Super Miners | 1025.8 | Pass | 1058.9 |
| 7 | Snakes | 1021.5 | Pass | 1014.5 |
| 8 | Sky Force | 980.7 | Pass | 973.2 |

Hooked On is the largest in this cold-route sample, 102.1 MiB above Tetris
and 128.4 MiB above Asphalt 3. The smaller games' order varies slightly
between runs, so this is not a fine-grained ranking of close workloads.
The cap covers only the test browser's renderer processes, not its browser,
GPU, or test-runner processes, and `memory.peak` is a cgroup charge rather
than whole-browser PSS. The three OOMs have nonzero `memory.events.oom_kill`;
all five passing routes completed their gameplay assertions with zero OOMs.

Raw results are under `/home/claude/.scratch/eka-tab-ram-rank-914c07b83-GAME/`
and `/home/claude/.scratch/eka-tab-ram-1843-914c07b83-GAME/`, except the
Asphalt 3 capped run, which is under
`/home/claude/.scratch/eka-tab-ram-1843-asphalt3-914c07b83/`. No benchmark
cgroups remained active after the runs.

## Installed-image launch under 1.8 GiB

The ranking above forced a fresh N-Gage installation in each browser. The
normal launcher already offered Hooked On a clean, source-hash-bound installed
filesystem image, but that image was not used by the ranking harness. Fresh
installation runs the N-Gage importer and compiles its guest code before
launching the game. The installed-image path in
[`shell.html`](../../emu/wasm/shell.html) restores the installed files before
emulator initialization and skips that work; it does not restore a CPU or
browser snapshot. The actual cold launcher selects the image when there is no
saved device manifest. Tetris and Asphalt 3 now have matching installed images
in `/home/claude/.local/share/eka2l1/preinstalled/`, outside Git. Hooked On's
existing image is unchanged.

A persistent-device Hooked On capture of the old installation path attributed
the largest sampled browser-native allocations to V8
`GetAndCopyFirstArgumentAsBytes` (360.5 MiB), `WasmCode` construction
(104.7 MiB), and trap-handler registration (66.1 MiB). These are retained
allocation *samples*, not additive PSS or directly avoidable byte counts.
The first site receives generated modules through
[`js_instantiate_aot_module`](../../emu/cpu/src/aot/aot_runtime.cpp). The
unprepared Hooked On client screen reported 113,233 compiled functions;
the prepared client screen reported 81,699 on the same WASM binary, 31,534
fewer translations. Avoiding installer execution therefore removes a large
source of V8 module/code allocations, as well as transient installer file
work. The sampled attribution is at
`/home/claude/.scratch/eka-hooked-1843-attribution-persistent/native-attribution.json`.

The cap remained **1,843 MiB**, swap disabled, on Chromium 153 with a fresh
profile per run. It applies to this test browser's renderer processes only.
Every passing result below reached gameplay, completed a continuous unpaced
window with guest sound enabled, and had zero kernel OOM events. The private
server used the **exact** live WASM SHA-256
`d4b9ca1f64cf9d3ac47443818be2589bfe9e7af3c4ea570bfa29662a44aec26c`;
the deployed `.lan` service serves that same binary and now advertises all
three image URLs.

| # | Game and gameplay check | Fresh-install 4,096 MiB control peak | Prepared private peaks | Prepared `.lan` peaks | Result at 1,843 MiB |
|---:|---|---:|---:|---:|---|
| 1 | Hooked On, Costa Rica boat | 2,066.7 MiB | — | 1,796.0 / 1,752.8 MiB | Pass twice |
| 2 | Tetris, Marathon board | 1,964.6 MiB | 1,815.6 / 1,742.9 MiB | 1,724.4 MiB | Pass three times |
| 3 | Asphalt 3, racing HUD | 1,938.2 MiB | 1,825.6 / 1,817.5 MiB | 1,733.7 MiB | Pass three times |

The fresh-install controls include importer work and are not a byte-for-byte
attribution of the peak difference. Tetris's unpaced gameplay window was five
host seconds in the accepted prepared runs; the old control used 15 seconds.
In one rejected 15-second prepared attempt, the game reached the board under
the cap but advanced guest time to the harness's 1,800-second ceiling before
the wall-clock window ended. That was a benchmark route error, not an OOM or
game fault. The accepted five-second windows kept the same Marathon board
assertion and stayed below the guest-time ceiling. The Hooked On and Asphalt 3
routes kept their original gameplay checks and window lengths.

The new installed files were captured only after the importer exited
successfully and the emulator closed its file handles. Tetris's image contains
9,781 files, 202,033,712 unpacked bytes and 108,482,012 compressed bytes;
its SHA-256 is `fc9fe5c169c49dcc98d43022a0b5a514b31eaf123fe93739653afeed2fa2ceae`.
Asphalt 3's image contains 9,803 files, 212,669,850 unpacked bytes and
117,994,255 compressed bytes; its SHA-256 is
`6cee9472f55a0b4dae0054f7ef2eb5c99f79f084a72e7811b11316b37188681c`.
The server checked both image manifests and hashes at startup. The normal
paced `.lan` Start flow separately restored both new images and reached
nontrivial screens without errors; the capped tests used the same restore code
and reached each game's verified gameplay scene. `installed-image.test.ts`
also passed its identity, exact-byte, invalid-path and truncation checks.

The compiler alternatives tested before correcting the launch-path mismatch
did not meet the cap: preloading a 24.4 MB compressed Hooked On ROM cache
(1,204 modules, 231 MiB raw WASM), a 256-byte region window, an inline-site
limit of four, and demand-successor batches of one, four and eight. The
one-entry batch cut the Hooked On client count to 67,111 functions but still
OOMed while instantiating many tiny modules. All experimental compiler
edits were reverted; no compiler setting or runtime WASM changed for this
result. The exact installed-image path is now the default for a new device in
the three supported N-Gage games. At this point in the work, a source install
still exceeded the cap; the staged source-install result follows below.

Raw capped results are in `/home/claude/.scratch/eka-1843-live-prepared-hooked-cap/`,
`eka-1843-live-hooked-cap-r2/`, `eka-1843-prepared-tetris-5s-cap/`,
`eka-1843-prepared-tetris-5s-cap-r2/`, `eka-1843-live-tetris-cap/`,
`eka-1843-prepared-asphalt3-cap/`, `eka-1843-prepared-asphalt3-cap-r2/`, and
`eka-1843-live-asphalt3-cap/`. Captures and hashes for the two new images are
under `/home/claude/.scratch/eka-1843-{tetris,asphalt3}-prepared-capture/`.
Normal launcher captures are `eka-1843-live-ui-{tetris,asphalt3}/` in the same
scratch directory.

## Fresh installation under the same cap

The prepared images above remain the fastest normal first-launch path. A
source install now saves the device after firmware and runtime SIS installation,
opens a small three-second handoff page, runs the N-Gage importer in a fresh
emulator page, then saves and uses the same handoff before starting the game.
The handoffs terminate the old Emscripten workers and let Chromium release the
old WASM page before loading the next one. Large saved files use Blob-backed
IndexedDB records and stream back into WasmFS in chunks. No game files or
firmware were replaced with a prepared image during these runs.

The tests used the same `d4b9ca1f` WASM binary, a fresh Chromium profile per
run, the normal selected-game launcher with prepared images forced off, and a
renderer-only 1,843 MiB cgroup with no swap. Each pass includes the importer
exit and the gameplay assertion. Tetris uses the five-second Marathon window;
the other gameplay windows are unchanged. Peaks include all install stages
and the final game in one browser lifetime.

| # | Game and gameplay check | Old one-page fresh result | Staged fresh peak MiB | Headroom MiB | Result |
|---:|---|---|---:|---:|---|
| 1 | Hooked On, Costa Rica fishing | OOM at 1,843 MiB | 1,794.1 | 48.9 | Pass |
| 2 | Tetris, Marathon board | OOM at 1,843 MiB | 1,733.0 | 110.0 | Pass |
| 3 | Asphalt 3, racing HUD, run 1 | OOM at 1,843 MiB | 1,798.3 | 44.7 | Pass |
| 4 | Asphalt 3, racing HUD, run 2 | OOM at 1,843 MiB | 1,724.6 | 118.4 | Pass |
| 5 | Asphalt 3, racing HUD, deployed `.lan` | OOM at 1,843 MiB | 1,712.7 | 130.3 | Pass |

The old fresh controls without a cap reached 2,066.7 MiB for Hooked On,
1,964.6 MiB for Tetris and 1,938.2 MiB for Asphalt 3. These are historical
runs, so the peak differences above are not a byte-exact attribution to any
single change. In the new Asphalt runs, sampled importer peaks were about
1,703 and 1,714 MiB. A single-handoff trial reached 1,822.3 MiB, leaving
only 20.7 MiB of headroom; the first handoff reduced the importer peak in the
subsequent runs. The new path adds six seconds of interstitial time only to a
fresh install. Prepared-image launches do not use it.

The raw final runs are under
`/home/claude/.scratch/eka-fresh-under-1843/{hooked,tetris,asphalt}-two-handoffs-cap-r*/`.
Earlier attempts and diagnostic captures remain in the same parent directory.
The deployed HTTPS capture is `asphalt-lan-final-cap/` there. `.lan` serves
the same WASM and JavaScript hashes listed above, plus the staged launcher and
Blob-backed persistence from release `d4b9ca1f-ce1fdfbbf`.
One diagnostic Asphalt run stalled before OPFS reported ready, at about 155 MiB
renderer usage, and was stopped; it was not an OOM or an accepted install run.

## Reclaim installer code while the guest keeps running

The two page handoffs above freed browser memory by destroying the entire
emulator page. The current source-install path keeps the page, guest kernel,
processes, and filesystem alive. At a CPU batch boundary it clears compiled
function indexes, exits and destroys only the CPU Web Worker, then starts a new
worker against the same guest state. This releases V8 code owned by the old
worker. Hooked On does this once during its unusually large importer, after
50,000 compiled functions, and again when the importer exits. Tetris and
Asphalt 3 recycle once at importer exit. A compiled slice no longer advances
unpaced guest time solely because translation occurred, which prevents the
importer's guest timeout while the new worker warms its code.

Chrome 153 ran the same fresh source-install and gameplay routes with sound
enabled, a fresh profile per run, a renderer-only 1,843 MiB limit and no swap.
The no-recycle controls used this WASM binary with the benchmark's recycle
switch disabled and a 4,096 MiB limit. All passing in-page runs had exactly
one main-page navigation, zero OOM events, and their usual gameplay assertion.

| # | Game | No-recycle peak, MiB | Old page-handoff peak, MiB | In-page peak, MiB | Gameplay |
|---:|---|---:|---:|---:|---|
| 1 | Hooked On | 2,087.3 | 1,794.1 | 1,676.7 / 1,643.4 / 1,629.9 / 1,676.1 | Costa Rica boat, four passes |
| 2 | Tetris | 2,089.1 | 1,733.0 | 1,737.0 | Marathon board, pass |
| 3 | Asphalt 3 | 1,973.1 | 1,712.7 | 1,692.8 | Racing HUD, pass |

The old page-handoff numbers came from the previous build and are a historical
comparison, not paired allocation savings. The in-page Hooked On runs have
166.3 to 213.1 MiB of headroom. A trial that recycled only after its importer
passed gameplay but touched the cap twice, so the earlier recycle is necessary
for a reliable margin. The no-recycle controls reached the same gameplay
scenes. Their measured unpaced FPS versus the in-page runs was 213.6 versus
214.4 for Hooked On, 128.2 versus 153.9 for Tetris, and 122.4 versus 117.7
for Asphalt 3. CPU frequency varied; these single windows do not establish a
speedup or a material steady-play regression. The post-install compile cache
must warm again, so first launch can still take longer than a no-recycle run.

From first navigation to comparable scenes, the in-page runs reached the
Tetris board in 76.6 s, Asphalt 3's warm race in 87.8 s, and Hooked On's boat
in 147.2 s. The old two-handoff captures took 89.7 s, 104.3 s, and 150.4 s,
respectively. These are one-off startup timings with host-load variation, but
they show no obvious loss against the former memory-saving path. Prepared-image
launches bypass the importer and do not recycle the CPU worker.

The raw results are under `/home/claude/.scratch/eka-no-reboot-20261018/`:
`tetris-integrated-cap/`, `asphalt3-integrated-cap/`,
`hooked-early-recycle-cap{,-r2}/`, `hooked-final-cap/`, `hooked-release-cap/`, and the matching `*-matched-control/`
directories (Asphalt 3 uses `asphalt3-matched-control-r2/`; the Hooked On
control is `hooked-final-control/`). The latter preceded only an unused
failure-cleanup edit and a status-line correction in the final JavaScript. Each route's
`navigations.jsonl`, `result.json`, screenshots and browser log are retained.
The released WASM SHA-256 is
`fa0634ef6f34e1e50bdf46a54494cfa7672a33267ecba4960764cde940e5701c`;
the released JavaScript SHA-256 is
`d090419cf22739b2697a03724852f3de68ebad571a9593bce2661bbac68c5f13`.
The live `.lan` service served those exact hashes. A forced fresh Tetris install
on the released build completed with one page navigation, two installed
entries, an active save timer, and the Running status after CPU worker recycle;
the browser reported no page errors.

## 1.5 GiB target on the later live build

On 2026-10-19, `.lan` served WASM SHA-256
`efa1637fadf33a9bd6ff09eb2bb3a705870008a3110a998a07c012ccbee0be69`.
The renderer-only cap was lowered to exactly 1,536 MiB, with swap disabled.
The three games that had dominated earlier measurements were run through the
forced fresh-install launcher in a new Chromium profile each time. A separate
4,096 MiB run of each game reached its verified gameplay scene without hitting
that higher cap; a 1,536 MiB run then checked the actual kernel outcome.

| # | Game | 4,096 MiB control peak (MiB) | 1,536 MiB result |
|---:|---|---:|---|
| 1 | Tetris | 1717.3 | Kernel OOM before gameplay |
| 2 | Asphalt 3 | 1716.6 | Kernel OOM before gameplay |
| 3 | Hooked On | 1637.1 | Kernel OOM before gameplay |

Tetris exceeded Asphalt 3 by only 0.7 MiB in these single cold runs. That is
too small to establish a stable order; both are the leading memory workloads
in this checked set. Hooked On was 79.5 MiB below Asphalt 3. The five games
that were much lighter on the previous build were not rerun on this binary.
Every capped run had `memory.events.oom_kill=1`; the 1,536 MiB peak is censored
by the cap and is not an estimate of unrestricted demand. The cgroup includes
the test browser's renderer processes, not its browser and GPU processes.

The matched controls are under
`/home/claude/.scratch/eka-tab-ram-rank-efa163-GAME/`, and the capped runs
under `/home/claude/.scratch/eka-tab-ram-1536-efa163-GAME/`. The same three
games also OOMed at 1,536 MiB on the preceding `fa0634ef` WASM, but their
peaks are not mixed into this build-matched ranking. No benchmark cgroups
remained active after the runs.

## Fresh installation below 1.5 GiB without unpaced FPS loss

The next browser build (`eka2l1.wasm` SHA-256
`5553463c4ef2b20d05208ecdf3f1bcc566c6ef383037cfaff2221172b208e8a7`)
was built with `EKA2L1_AOT_SOURCE_MAPS=OFF` (the repository default) and
`EKA2L1_WASM_CPP_SOURCE_MAPS=ON`, and uses 8 MiB linear-memory growth steps.
Guest-source-map profiling can use a separate mapped build. Fresh N-Gage
installation recycles the CPU worker when each game's configured
compiled-function threshold is reached;
Tetris and Hooked On also recycle during startup warmup. For Asphalt 3, the
launcher releases only Emscripten workers that have returned to its unused
pool during a 30-second startup window. The four running workers remain
attached, and the cleanup timer ends before the measured race. The game-load
CPU-worker recycle experiment was rejected after a same-binary Asphalt 3
comparison measured 99.9 versus 107.0 FPS.

Each capped run below forced a fresh installation in a new Chromium profile,
used guest audio, a renderer-only 1,536 MiB cgroup, and zero swap. Every route
reached its verified gameplay scene with `oom=0`, `oom_kill=0`, and
`memory.max=0`.

| # | Game | Prior unrestricted peak, MiB | New capped peak, MiB | Unpaced gameplay FPS |
|---:|---|---:|---:|---:|
| 1 | Tetris | 1717.3 | 1439.0 | 136.1 (host fell from 3.6 to 3.0 GHz) |
| 2 | Asphalt 3 | 1716.6 | 1474.0 / 1468.1 | 120.4 at 3.6 GHz / 94.2 while host rose from 3.2 to 3.6 GHz |
| 3 | Hooked On | 1637.1 | 1324.7 | 225.1 (host clock varied) |

For a controlled Tetris throughput check, the saved pre-change build and the
new build ran the same prepared-image unpaced route in alternating order at a
fixed 3.0 GHz. Their FPS were 165.3/163.2 and 164.1/165.5 respectively.
These prepared-image measurements isolate steady gameplay; the capped rows
above provide the separate fresh-install acceptance evidence. Hooked On's new
225.1 FPS lies within its earlier 220.7–289.9 FPS range. Asphalt 3's new
steady-clock 120.4 FPS exceeds the 108.0 FPS no-startup-cleanup control at
3.6 GHz. Across these checks there is no measured unpaced FPS loss.

The corresponding raw runs are under `/home/claude/.scratch/eka-1536-integrated-`
for each game and Asphalt 3 repeat, and
`/home/claude/.scratch/eka-tetris-fixed3g-{control,integrated}-r{1,2}/`.

## Tab-only 1.5 GiB cap and retained all-renderer result

The earlier cap placed **every Chromium renderer** in one cgroup. That includes
the game's renderer, Chrome's UI renderer, and a spare renderer. A later exact
`.lan` Asphalt 3 run completed its race at 120.6 unpaced FPS but touched the
1,536 MiB all-renderer cap (`memory.max=3`, `oom=0`). Thus the earlier passing
all-renderer runs did not establish repeatable headroom. Raw evidence is in
`/home/claude/.scratch/eka-1536-lan-asphalt3-final/`.
With the new harness but its default all-renderer scope, another fresh
Asphalt route completed under a 4,096 MiB control limit and peaked at
1,538.3 MiB with all four renderer processes charged. This independently
confirms that the all-renderer workload can exceed 1.5 GiB.

The benchmark now has a separate **game-tab renderer** scope. With
`EKA2L1_TAB_RENDERER_CLIENT_ID=7`, the renderer prefix puts only that process
in the memory-limited, zero-swap cgroup from process creation. The route
checks that it is the uniquely largest renderer after runtime startup and
that the other renderers are outside the capped cgroup. The client ID is
specific to this Chromium 154 launch; a changed ID causes an explicit route
failure. The original all-renderer mode remains the default when the variable
is unset. The game-tab limit excludes the browser and GPU processes, as did
the previous all-renderer limit. It measures a different, narrower scope; it
does not reduce the emulator's actual memory use.

The exact committed `.lan` WASM (`5553463c4ef2b20d05208ecdf3f1bcc566c6ef383037cfaff2221172b208e8a7`)
completed the fresh-install, guest-audio route in a new browser profile for
each row. Every row had `memory.max=0`, `oom=0`, and `oom_kill=0`, reached its
asserted gameplay scene, and completed its continuous unpaced window. CPU
clocks varied, so the FPS column is evidence of playability, not a speed
comparison between rows.

| # | Game and run | Tab peak, MiB | Headroom, MiB | Unpaced FPS | Verified scene |
|---:|---|---:|---:|---:|---|
| 1 | Asphalt 3, run 1 | 1442.4 | 93.6 | 84.2 | Racing HUD |
| 2 | Asphalt 3, run 2 | 1398.3 | 137.7 | 115.9 | Racing HUD |
| 3 | Asphalt 3, run 3 | 1439.9 | 96.1 | 129.3 | Racing HUD |
| 4 | Asphalt 3, run 4, strict cgroup assertion | 1432.9 | 103.1 | 102.9 | Racing HUD |
| 5 | Tetris | 1390.1 | 145.9 | 165.2 | Marathon board |
| 6 | Hooked On | 1265.6 | 270.4 | 226.9 | Costa Rica boat |

These measurements are in `/home/claude/.scratch/eka-1536-tabonly-` with
game/run suffixes. The fourth Asphalt run records the selected renderer and
the other renderers' distinct cgroups in `route/tab-renderer.json`. The
production WASM and its execution path were unchanged, so this measurement
correction entails no unpaced FPS tradeoff.

The all-renderer regression capture is at
`/home/claude/.scratch/eka-4096-all-renderer-harness-regression/`. A separate
non-fresh Snakes harness attempt stopped at an OPFS `NotFoundError` before
gameplay; it supplies no Snakes memory or FPS result and is not counted here.

Other memory-policy experiments were rejected before changing the release:

| # | Experiment | All-renderer Asphalt peak, MiB | Throughput observation | Decision |
|---:|---|---:|---|---|
| 1 | Merge cold modules at 128 functions | 1460.5; second run OOM at 1536 | First run 118.7 FPS | Rejected for OOM |
| 2 | Extra 30,000-function startup CPU recycle | 1386.0 / 1509.7 | Fixed 3.0 GHz: 80.1 / 77.9 FPS versus 82.9 / 85.1 controls | Rejected for FPS loss and variable memory gain |
| 3 | Eager-compile entry points without static successor walk | 1486.3 | 114.6 FPS at about 3.6 GHz in one run | Rejected for weak memory headroom |
| 4 | 4 MiB WASM memory-growth step | 1507.3 / 1500.4 | 119.9 / 118.6 FPS with varying clocks | Rejected for weak memory headroom |
| 5 | Merge cold modules at 32 functions | 1523.6 | 114.5 FPS in one run | Rejected for weak memory headroom |
| 6 | Emscripten `emmalloc` | 1491.2 / 1516.8 | Clocks varied; no comparable FPS claim | Rejected for weak memory headroom |

The one-run FPS figures above do not establish a throughput change. The
fixed-clock pair in row 2 does, and is why that otherwise promising recycle
policy was not kept. No experimental emulator setting in this table is in the
committed release.
