# Qt versus WASM: unpaced FPS for the ten launcher games

Measured on 2026-10-12 after the Thumb loop changes in `fdbd53c9f`.
All results below are fresh measurements on the same Intel Core i7-10875H
laptop, using the NVIDIA Quadro T1000 Max-Q for both frontends. Nine games
have valid native/browser pairs. Asphalt 3 ran in the browser but failed to
reach gameplay in four native unpaced attempts; its Qt FPS is unavailable.

FPS means **screen presentations per host wall-clock second**, including
identical images. These are unpaced observations, not display refresh rates,
unique-frame counts, or statistically established speedups. Native uses an
instruction-driven virtual clock; WASM advances its count-free clock at yields
and idle events. The results therefore do not establish identical guest work
per frame or cycle-accurate relative throughput.

## Results

| # | Game | Qt FPS | WASM FPS | WASM / Qt | Window seconds, Qt / WASM |
|---|---|---:|---:|---:|---:|
| 1 | Snakes | 106.71 | 95.12 | 89.1% | 5.00 / 5.03 |
| 2 | Sky Force | 291.72 | 310.75 | 106.5% | 2.00 / 2.01 |
| 3 | K-Rally | 199.48 | 174.01 | 87.2% | 3.00 / 3.02 |
| 4 | Asphalt: Urban GT 2 | 175.74 | 140.21 | 79.8% | 10.01 / 10.05 |
| 5 | Hooked On: Creatures of the Deep | 342.93 | 269.10 | 78.5% | 5.00 / 5.05 |
| 6 | Asphalt 3: Street Rules | failed | 69.05 | — | — / 5.04 |
| 7 | Tetris | 159.26 | 168.91 | 106.1% | 5.00 / 5.04 |
| 8 | Super Miners | 327.34 | 353.27 | 107.9% | 5.00 / 5.02 |
| 9 | Explode Arena | 429.70 | 274.18 | 63.8% | 2.00 / 3.01 |
| 10 | Pathway to Glory | 244.19 | 143.11 | 58.6% | 5.00 / 5.02 |

The largest observed browser deficits are Pathway and Explode Arena. WASM
has a higher observed rate for Sky Force, Tetris and Super Miners. Short samples,
different clocks and scene variation limit those comparisons; they are leads
for matched profiling, not explanations of the gaps. The earlier
[paced comparison](QT_VS_WASM_PACED_FPS.md) measures a different operating mode.

## Scenes and acceptance

- Snakes: new opening level, 60-second timer, score 200, no movement input.
- Sky Force: red ship in stage 1, auto-fire, opening clouds and islands.
- K-Rally: opening tutorial track, holding accelerate (4) and right steering.
  Car positions and headings differ at the endpoints.
- Asphalt 2: Arcade / Single Race, red Golf GTI MkV, San Francisco, first outside
  barrier without steering. This preserves the vehicle/course used in the
  recent optimization reports; it is not an exact input replay.
- Hooked On: Costa Rica boat map, holding Up toward the coast. Introductory
  help transitions are included; this is not a fishing battle.
- Asphalt 3: Instant Play, red car on the coastal track, holding Up. Sound was
  disabled at the game's prompt. Qt attempts failed with a null read at `0x8`
  (`PC=0x74a09eac`, `KERN-EXEC 3`), a `USER 44` panic while loading `carsb.bar`,
  or the N-Gage launcher's `-1000002` error. Fresh installed-state copies and
  tighter input timing did not resolve this. The cause is not diagnosed, and
  this is not evidence that ordinary paced Qt cannot run the game.
- Tetris: Marathon level 1, initial left/rotate input, then falling pieces.
- Super Miners: challenge level 1, one short left input, then mostly stationary
  player with an active timer.
- Explode Arena: first arcade level, short left input, enemies active. Both
  accepted endpoints show gameplay and a living player. A longer native window
  reached the menu and was rejected.
- Pathway to Glory: Pantelleria battlefield after movement, awaiting the next
  action. Selected soldier and viewport differ; this is not a combat sequence.

Loading and menu navigation are excluded. Screenshots outside each accepted
window establish the scene at its endpoints. The games advance rapidly when
unpaced, so windows are deliberately short to avoid death screens and dialogs.
This is one accepted window per game/frontend, not a repeated benchmark with
confidence intervals. Rejections were based on scene or harness validity,
not choosing the highest FPS. Full-game completion and audio correctness were
not tested. The N-Gage client still labels installed packages TRIAL; package
identity alone does not prove activation.

## Measurement and clocks

Native Qt was rebuilt from the current source with the small interactive
benchmark change accompanying this report. It uses Dynarmic, native Xorg,
NVIDIA PRIME OpenGL and disabled host vsync. An `LD_PRELOAD` wrapper records
completed `glXSwapBuffers` timestamps using `CLOCK_MONOTONIC`; every accepted
window contains one game drawable. The virtual clock advances from native
instruction counts, with the existing 4,840-instruction maximum dispatch,
and jumps to the next event when idle. Benchmark presentation synchronization
remains enabled. This is an opt-in throughput harness, not normal desktop play.

WASM ran in Chromium 153.0.8010.52 with hardware ANGLE/Vulkan. The custom
launcher on `https://claude-laptop.lan:8188/` loaded the same manifest packages
from the local cache and N-Gage/card preload endpoints. Its compiled build and
ordinary optimization defaults were unchanged. Route deadlines controlled
startup navigation; the timed windows ran continuously toward a distant
1,800-second guest deadline. Every sampled route phase was 0 and instruction
total was 0. FPS uses `eka2l1_presentations` and browser `performance.now()`.
No profiler was attached. Residual emulator translation and engine JIT work
remain in the timed windows; the JSON records WASM compilation deltas.

Only one emulator executed during each timed window. Native processes were
suspended between navigation commands and browser route pauses held the other
frontend. Loading preparation could overlap, but accepted timing did not.
Background host work was not disabled; load averages are retained in the raw
records. Both frontends ran shared guest-clock audio DSP with output discarded
or muted, except Asphalt 3's explicitly disabled game sound.

The native audio factory initially retained replay PCM despite the interactive
flag, imposing extra retention and a 120-guest-second limit. It now honors the
existing `retain_audio` and `interactive` settings. All native samples taken
with that initial build were excluded and rerun with the final binary. The
accepted Asphalt 2 run proceeds beyond that old limit. Ordinary paced desktop
audio and deterministic replay defaults remain unchanged.

## Reproduce the native mode

Use a private copy of installed emulator data, a hardware-accelerated display,
and a game installed from [games.json](../wasm/games.json):

```sh
EKA2L1_BENCHMARK=1 EKA2L1_QT_UNPACED=1 \
EKA2L1_QT_PROFILE_CPU=dynarmic EKA2L1_SHARED_AUDIO=1 \
__GL_SYNC_TO_VBLANK=0 build/bin/eka2l1_qt --run 0x2000730f
```

`EKA2L1_QT_UNPACED=1` enables live keyboard/mouse input and disables replay
artifact retention inside the benchmark. It does not enable the profiler.
The explicit CPU setting selects Dynarmic; without it the existing deterministic
DynCom reference remains selected. Leave all benchmark variables unset for
ordinary paced Qt. Qt's on-screen FPS uses guest time in this mode; measure
completed presentations against host time instead.

## Builds, evidence and verification

- Source: `fdbd53c9f` on `wasm-port`, plus the native interactive harness changes
  in this report's commit.
- Final Qt binary: `build/bin/eka2l1_qt`, SHA-256
  `e0d56a07dab199dce46630898bcdca0da027763605216c8924e66b07797a6215`.
- Served WASM: SHA-256
  `72458db489c3d5502c8372fb341e5bf8a313ca9ccf00434fdb75c947f84c2c9b`,
  verified again after the campaign. The LAN service was not changed.
- [Per-game measurements and metadata](qt-vs-wasm-unpaced-fps.json) include
  versions, durations, frame counts, guest-clock progress, compilation,
  screenshot paths and rejected attempts. Guest-clock progress is diagnostic,
  not a second speedup metric.
- Raw logs, screenshots and input commands:
  `/home/claude/.scratch/eka-unpaced-ten-20261012/`. Harnesses are `qt.py`,
  `swaps.c`, `wasm.mjs`, `qt-steps.log` and per-browser `steps.log` files.
  The accepted Snakes browser sample is in `wasm-retry`.
- All 17 manifest-referenced cached payload hashes were checked. Firmware and
  game blobs were not committed. Native uses private copies of installed state;
  startup saves therefore need not match a fresh browser install.
- Native build and CTest passed (3/3). The actual Qt/browser gameplay paths
  produced 19 valid windows; the missing twentieth is explicitly reported.

Test emulator/browser processes were closed, the original VT restored, and
temporary Xorg credentials removed. No `/dev/uinput` permissions were changed.
