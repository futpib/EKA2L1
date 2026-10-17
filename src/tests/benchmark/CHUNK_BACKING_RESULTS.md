# Back guest chunks by their requested maximum

The multiple memory model reserves guest addresses in whole page-table slots
(1 MiB with 4 KiB pages). Its WASM host allocation used to have the same size,
even when a chunk's requested maximum was much smaller. The Tetris startup
trace created 246 chunks with requested maxima below 1 MiB. That count includes
chunks that may later be destroyed or use another backing path; it is not a
retained-memory total.

The WASM chunk now keeps the full guest address reservation but allocates host
backing only through the page-rounded requested maximum. `adjust` rejects
growth past that maximum. Deletion releases the full address reservation.
Native host backing remains unchanged.

## Matched browser comparison

Both binaries came from one isolated Emscripten build at `2cb9712ff` plus this
change. The control object changed only the assignment of the host-backed
maximum back to the full reserved size; all other objects, link settings,
assets and game routes were identical. The candidate WASM SHA-256 is
`15d10c7ef0585b310bf3410ff5478bc2ba69d2c2a884c01d4b88fcb3b2e73166`;
the matched control is
`89b6bc608ad5dae68f0acdb18a9f0f732ab70faf7bdb0ebe5d704e4be08e3fa8`.
The candidate was relinked after the control and reproduced its original hash.

Fresh Chromium sessions ran the stock Tetris route through a playable board,
then measured its 15-second unpaced gameplay window with guest sound enabled.
All runs had no game or browser errors.

| # | Binary and run | WASM capacity MiB | Whole-browser PSS MiB at window end | Observed PSS peak MiB | FPS |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Matched control | 1617.25 | 2707.30 | 2779.32 | 129.68 |
| 2 | Candidate, first run | 1528.94 | 2605.42 | 2609.32 | 145.68 |
| 3 | Candidate, second run | 1529.12 | 2564.45 | 2661.78 | 137.17 |

The repeatable result is about **88 MiB less WASM capacity** at the Tetris
scene. Whole-browser PSS was 102–143 MiB lower at the window end, but browser
process and V8 memory vary between fresh sessions, so this is supporting
evidence rather than exact per-allocation physical attribution. Host CPU
frequencies differed between runs; these samples do not establish an FPS gain.
The code change does not add a runtime access-path check.

The full `test_aot_wasm.js` suite passed 197 tests, including a new test that a
16 KiB ROM chunk retains a separate 1 MiB guest address slot, maps its requested
pages and rejects growth beyond its maximum. Candidate browser routes reached
gameplay without errors for Snakes, Sky Force, K-Rally, Asphalt: Urban GT 2,
Hooked On, Asphalt 3, Tetris and Super Miners. The standalone N-Gage client also
reached its library route without errors. The browser evidence is retained under
`/home/claude/.scratch/eka-chunk-bench-*` and the matched binaries under
`/home/claude/.scratch/eka-chunk-matched-control/`.
