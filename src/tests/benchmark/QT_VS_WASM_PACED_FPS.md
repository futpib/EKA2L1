# Qt versus WASM: paced FPS for the ten launcher games

Measured on 2026-10-11 on the same Intel Core i7-10875H laptop. These are mean screen presentations per host wall-clock second during short gameplay samples. Both frontends used normal paced operation. They are not unpaced throughput measurements or counts of unique images.

All ten native Qt results and the first five browser results were measured for this comparison. Browser results for games six through ten reuse the immediately preceding campaign on the same unchanged deployed WASM binary. Game and firmware packages match the launcher manifest.

| # | Game | Qt FPS | WASM FPS | Sample |
|---|---|---:|---:|---|
| 1 | Snakes | 15.96 | 16.02 | Opening level |
| 2 | Sky Force | 31.99 | 31.96 | Stage 1 |
| 3 | K-Rally | 32.03 | 31.98 | Tutorial track |
| 4 | Asphalt: Urban GT 2 | 15.60 | 10.85 | Instant Play; different vehicle/route |
| 5 | Hooked On: Creatures of the Deep | 31.66 | 26.00 | Costa Rica boat map |
| 6 | Asphalt 3: Street Rules | 29.73 | 31.62 | Instant Play; collision/stopped car |
| 7 | Tetris | 30.26 | 28.37 | Marathon level 1 |
| 8 | Super Miners | 31.99 | 31.97 | Challenge level 1 |
| 9 | Explode Arena | 34.53 | 38.07 | First arcade level |
| 10 | Pathway to Glory | 35.99 | 39.98 | Pantelleria; waiting for next action |

## How the numbers were measured

- Qt: current native build, default Dynarmic backend, no benchmark environment. An `LD_PRELOAD` wrapper timestamps completed `glXSwapBuffers` calls with `CLOCK_MONOTONIC`; each accepted window contained exactly one game drawable. Hardware-accelerated native Xorg used Intel UHD graphics. Audio ran through an isolated PipeWire null sink.
- WASM: Chromium 153.0.8010.52, hardware ANGLE/Vulkan on NVIDIA Quadro T1000 Max-Q. Games launched through the ordinary LAN picker, using its paced count-free defaults. FPS is the change in `eka2l1_presentations` divided by browser `performance.now()` elapsed time. Route phase stayed -1, instruction counts stayed zero, and guest time advanced at approximately 1x host time. Browser audio output was muted.
- Screenshots taken outside each timed window establish the scene at both endpoints. Native windows are 30 seconds except Explode Arena (15 seconds). Browser windows are generally 30 seconds, with the shorter accepted Snakes, Sky Force and Explode Arena windows specified in the JSON.
- One EKA2L1 instance ran at a time during sampling. Background host work was left running; substantial Android build/emulator activity overlapped part of the campaign, including the Qt Pathway sample. Load averages are recorded per row. These are observations on a shared host, not controlled statistical speedup estimates.

## Interpretation and scene limits

Snakes, Sky Force, K-Rally and Super Miners are close to the same paced presentation rates in both frontends. Hooked On and Tetris were lower in the browser samples. Treat differences in the other rows cautiously: the scenes, graphics backends and host load were not identical.

Asphalt 2 Instant Play randomized the vehicle and route: Qt used a motorcycle on the city course, while WASM used a red car on a bridge/coastal course. Its row is a rough per-game observation, not a matched-scene performance comparison. Asphalt 3 ended with the car stationary after a collision on both frontends; its row does not establish sustained racing performance.

K-Rally held digit 4 and right steering on the opening tutorial track, ending stopped near the start/finish area. Super Miners had an active level timer with a mostly stationary player. Pathway was awaiting the next turn action after movement, with a different selected soldier/camera position; it was not a combat sequence. Explode Arena used a shorter window with enemies active and the player alive at both endpoints.

Hooked On includes introductory map help transitions, not a fishing battle. Its browser menu began accepting Select after Back to Game (F2), then Enter. Package identity does not establish full-game activation: the N-Gage client still labels the installed packages TRIAL. This campaign did not test audio correctness or full playthroughs.

The rejected Qt Pathway window entered a tutorial pause. Rejected browser windows captured a Snakes falling/death transition, a Snakes menu, and a Sky Force high-score screen. Their raw evidence is retained and excluded from the table. Repeats were selected by scene validity, not by the highest FPS.

## Builds and evidence

- Native binary was rebuilt with `cmake --build build --target eka2l1_qt -j4` from checkout `742f59668` on `wasm-port`; its generated version label still reads `8bea7f998`. SHA-256: `12adc27057e844a6a75140f71a24ec4ebd702068275ef0e2770cfe8627d9d9cb`.
- LAN WASM source commit: `86a64bdea` (later checkout changes were documentation). Served binary SHA-256 was verified unchanged: `dec37577f64f2b6addc7d3d1d9e877fe4bcd288bbc0b39022fd4c22236130f63`.
- Per-game versions, frame counts, durations, compilation time, host load, screenshot paths and rejected windows: [qt-vs-wasm-paced-fps.json](qt-vs-wasm-paced-fps.json).
- Game and firmware CIDs/hashes: [games.json](../wasm/games.json). All 17 referenced cached payloads were checked against their manifest SHA-256 hashes; installed native ROM hashes also matched.
- New raw artifacts, scripts and logs: `/home/claude/.scratch/eka-qt-wasm-ten-20261011/`. `qt.py`, `swaps.c`, `qt-steps.log`, `wasm.mjs`, and per-game `steps.log` files record the measurement and input sequences.
- Reused browser artifacts: `/home/claude/.scratch/eka-next-five-20261011/fps-*`, summarized in [PRE_TOUCH_GAMES_6_TO_10.md](PRE_TOUCH_GAMES_6_TO_10.md).

Test Qt/browser processes and private audio services were closed afterward. The original VT was restored, temporary Xorg credentials removed, and `/dev/uinput` permissions left unchanged. The LAN service and game assets were not modified.
