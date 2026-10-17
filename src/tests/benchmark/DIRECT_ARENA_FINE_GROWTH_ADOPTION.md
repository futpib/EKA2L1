# Finer direct-arena growth after installer staging

The stock Nokia 5320 Asphalt 3 race retained 160 MiB of zeroed process-arena
prefixes for 9.85 MiB of currently committed direct guest chunks. The previous
1 MiB growth trial did not lower whole-WASM PSS while WasmFS installation
buffers were still being copied in memory. After OPFS installer staging landed,
the same growth policy was retested against the current default.

`multiple_mem_model_process::initialize_direct_range` now rounds the needed
contiguous prefix up to 1 MiB. The 48 MiB per-process reservation, stable host
pointers, generated direct-memory access guard and page-table fallback are
unchanged. New guest pages still get zeroed when committed, including pages
inside an already published prefix. The change only removes zeroing between
the last committed byte and the next geometric growth boundary.

Two serial control/candidate pairs used fresh Chromium 153 profiles, the same
private HTTP launcher, stock Nokia 5320 firmware, persistent OPFS disks, sound
enabled in the guest, and the existing unpaced Asphalt 3 route to the race HUD.
Both private bundles omitted the derived ROM translation cache. The control
WASM SHA-256 was `8812ae4b5646ff32ca5d50d683c02aae3388dfbfbec3d0572f2ae0801534d592`;
the candidate was `d4be6fab0c3151f047ba385b630c78b31df935e9477cf16ae8bf1fe49246d426`.
All four runs reached the race HUD without guest errors. Linux `smaps` PSS
counts resident browser process pages; virtual arena reservations are not
counted as retained RAM.

| # | Asphalt 3 run | Arena prefix, MiB | WASM PSS, MiB | Browser PSS, MiB | Five-second FPS |
|---:|---|---:|---:|---:|---:|
| 1 | Control A | 160 | 594.43 | 2601.49 | 60.12 |
| 2 | Candidate A | 121 | 554.42 | 2598.56 | 81.28 |
| 3 | Control B | 160 | 609.40 | 2615.20 | 93.28 |
| 4 | Candidate B | 121 | 583.10 | 2606.00 | 98.44 |

The paired WASM reductions were **40.01 and 26.30 MiB**. Whole-browser PSS
fell **2.93 and 9.20 MiB** in these pairs; V8 executable, sandbox and allocator
residency varied enough to mask most of the WASM reduction. Control FPS itself
varied from 60 to 93, so these short windows establish no precise speedup.
There was no observed race-throughput regression. Growth events increased from
34 to 118 by the race; startup timings also varied substantially between fresh
browser launches and do not establish a startup improvement or regression.

Snakes reached gameplay without guest errors in both builds. Its arena prefix
fell from 36 to 30 MiB and WASM PSS from 365.03 to 363.20 MiB. The single
browser-PSS pair was 1530.89/1568.40 MiB and its five-second FPS was
38.04/77.64. The browser increase came mainly from native allocator and V8
mappings in that one run, so it cannot be attributed to the arena change.

The WASM correctness suite checks exact ARM/Thumb memory behavior, stable
native pointers, process isolation, upper-range growth and fallback mappings.
Its old 4 MiB/16 MiB geometric-growth expectations were updated for 1 MiB
steps. The full suite passed **197 tests with zero failures**. The previously
documented crash-reproduction fixture remains an expected harness limitation,
not a passing comparison.

Raw captures are in `/home/claude/.scratch/arena-memory-20261017/` under
`control-asphalt-http`, `candidate-asphalt`, `control-asphalt-repeat`,
`candidate-asphalt-repeat`, `control-snakes` and `candidate-snakes`. Each
contains the route report, HUD screenshot, browser log and process `smaps`.

## Live release

The release at `https://claude-laptop.lan:8188/` serves the same candidate
WASM SHA-256. A fresh Asphalt 3 ROM translation cache was exported for that
exact binary; its 34,458,040-byte blob has SHA-256
`ebc91cbcbdf5464a14a8d460827cccac67ce52b5e7680f2a964c876790da9cb6`.
The isolated release server accepted its manifest, and a browser run loaded
72,004 cached ROM functions, reached the race HUD, produced 463 frames in its
five-second unpaced window, and reported no guest errors. The live HTTPS
Snakes route reached gameplay, produced 356 frames in five seconds, reported
no guest errors, and retained a 30 MiB arena prefix. The downloaded live WASM
hash matched the release file, and the user service remained active after the
switch. Live-route captures are `release-cache-smoke` and `live-snakes` beside
the paired measurements.
