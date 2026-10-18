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
