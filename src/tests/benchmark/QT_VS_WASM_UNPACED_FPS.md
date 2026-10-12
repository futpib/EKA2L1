# Qt versus WASM: unpaced FPS for the ten launcher games

Measured on 2026-10-12 at `370409ce3`, after the Asphalt 3 audio fixes.
All ten games reached gameplay on both frontends. These are fresh measurements
on the same Intel Core i7-10875H laptop, using the NVIDIA Quadro T1000 Max-Q
for both frontends. Asphalt 3 has guest sound explicitly enabled on both.

FPS means **screen presentations per host wall-clock second**, including
identical images. These are unpaced observations, not display refresh rates,
unique-frame counts, or statistically established speedups. Native uses an
instruction-driven virtual clock; WASM advances its count-free clock at yields
and idle events. The results therefore do not establish identical guest work
per frame or cycle-accurate relative throughput.

## Results

| # | Game | Qt FPS | WASM FPS | WASM / Qt | Window seconds, Qt / WASM |
|---|---|---:|---:|---:|---:|
| 1 | Snakes | 105.70 | 97.80 | 92.5% | 5.00 / 5.02 |
| 2 | Sky Force | 287.72 | 331.79 | 115.3% | 2.00 / 2.01 |
| 3 | K-Rally | 200.15 | 184.72 | 92.3% | 3.00 / 3.02 |
| 4 | Asphalt: Urban GT 2 | 174.55 | 130.15 | 74.6% | 10.01 / 10.04 |
| 5 | Hooked On: Creatures of the Deep | 361.06 | 264.49 | 73.3% | 5.00 / 5.05 |
| 6 | Asphalt 3: Street Rules | 244.60 | 161.86 | 66.2% | 5.00 / 5.04 |
| 7 | Tetris | 171.44 | 169.48 | 98.9% | 5.00 / 5.04 |
| 8 | Super Miners | 328.51 | 352.10* | 107.2% | 5.00 / 5.02 |
| 9 | Explode Arena | 426.11 | 309.77 | 72.7% | 2.00 / 3.01 |
| 10 | Pathway to Glory | 232.39 | 140.09 | 60.3% | 5.00 / 5.03 |

*Super Miners is the median of three fresh browser launches: **244.26, 359.39,
352.10 FPS**. All three reached the same scene and passed the measurement checks;
none was discarded. The first low result prompted the two repeats. The cause
of the throughput variation is unresolved, so the median alone should not be treated as a
stable advantage over Qt. Each repeat used a five-second window. Other rows
have one accepted window per frontend.

Pathway has the largest observed browser deficit, followed by Asphalt 3.
Sky Force has a higher observed browser rate and Tetris is nearly equal.
Short samples, different clocks and scene variation limit those comparisons;
they are leads for matched profiling, not explanations of the gaps. The earlier
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
- Asphalt 3: Instant Play, red car on the coastal track, holding Up; YES selected
  at the sound prompt. Both windows start during the race's GO sequence and end
  at a barrier. Qt's first launch from an older installed-state copy failed
  with N-Gage error `-1000002` before gameplay. A fresh copy of the clean
  installation used in the audio-fix verification reached the race and supplied
  the accepted sample. The failed launch is retained without an FPS value;
  its cause was not diagnosed in this campaign.
- Tetris: Marathon level 1, initial left/rotate input, then falling pieces.
  Piece sequence and score differ between the frontends.
- Super Miners: challenge level 1, one short left input, then mostly stationary
  player with an active timer.
- Explode Arena: first arcade level, short left input, enemies active. Both
  accepted endpoints show gameplay and a living player.
- Pathway to Glory: Pantelleria battlefield after movement, awaiting the next
  action. Selected soldier and viewport differ; this is not a combat sequence.

Loading and menu navigation are excluded. Screenshots outside each accepted
window establish the scene at its endpoints. The games advance rapidly when
unpaced, so windows are deliberately short to avoid death screens and dialogs.
These observations do not provide confidence intervals. Full-game completion
and audible output quality were not tested. The N-Gage client still labels
installed packages TRIAL; package identity alone does not prove activation.

## Measurement and clocks

Native Qt uses Dynarmic, native Xorg, NVIDIA PRIME OpenGL and disabled host
vsync. An `LD_PRELOAD` wrapper records completed `glXSwapBuffers` timestamps
using `CLOCK_MONOTONIC`; every accepted window contains one game drawable.
The virtual clock advances from native instruction counts, with the existing
4,840-instruction maximum dispatch, and jumps to the next event when idle.
Benchmark presentation synchronization remains enabled. This is an opt-in
throughput harness, not normal desktop play. The existing interactive benchmark
build was used without source changes or a rebuild during this campaign.

WASM ran in Chromium 153.0.8010.52 with hardware ANGLE/Vulkan. The custom
launcher on `https://claude-laptop.lan:8188/` loaded the same manifest packages
from the local cache and N-Gage/card preload endpoints. Its compiled build and
ordinary optimization defaults were unchanged. Route deadlines controlled
startup navigation; the timed windows ran continuously toward a distant
1,800-second guest deadline. Every sampled route phase was 0 and instruction
total was 0. FPS uses `eka2l1_presentations` and browser `performance.now()`.
No profiler was attached. Residual emulator translation and engine JIT work
remain in the timed windows; the JSON records WASM compilation deltas.

Only one test emulator executed during each timed window. The first ten browser
runs completed before native runs began; the two Super Miners browser repeats
ran after native Qt closed. Native processes were suspended between navigation
commands. Background host work was not disabled; load averages are retained in
the raw records. Both frontends processed guest audio through their shared
clocked DSP, with host output muted in the browser and discarded in Qt.
Asphalt 3's guest sound was explicitly enabled; other games retained their
normal game settings. This does not repeat the audible-output validation in
[the audio-fix report](ASPHALT3_AUDIO.md).

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

- Source: `370409ce3` on `wasm-port`. No emulator code changed in this campaign.
- Qt binary: `build/bin/eka2l1_qt`, SHA-256
  `e958639777e92d96e4d5bf0162fc9090aba065cab1be1784e3c360f867db6fe0`.
- Served WASM: SHA-256
  `ad02135c672a175de7fb48e17b10df17cfdfc70385278002996599b31ff13284`,
  verified against the LAN response. The LAN service was not changed.
- [Per-game measurements and metadata](qt-vs-wasm-unpaced-fps.json) include
  versions, durations, frame counts, guest-clock progress, compilation,
  screenshot paths, all three Super Miners browser samples and the rejected
  native Asphalt 3 startup. Guest-clock progress is diagnostic, not a second
  speedup metric.
- Raw logs, screenshots and input commands:
  `/home/claude/.scratch/eka-unpaced-ten-after-audio-20261012/`. Harnesses are
  `qt.py`, `swaps.c`, `wasm.mjs`, `routes.json`, `qt-steps.log` and per-browser
  `steps.log` files. The previous report and JSON are retained there too.
- All 17 manifest-referenced cached payload hashes were rechecked and match the
  previous campaign. Firmware and game blobs were not committed. Native uses
  private copies of installed state; startup saves therefore need not match a
  fresh browser install.
- Actual Qt/browser gameplay produced all 20 paired windows, plus two additional
  valid browser windows for Super Miners. Screenshots were reviewed, FPS
  arithmetic and counter invariants checked, and accepted logs contained no
  captured access violation, kernel panic or abort. No rebuild was needed for
  this measurement-only update.

Test emulator/browser processes were closed, the original VT restored, and
temporary Xorg credentials removed. No `/dev/uinput` permissions were changed.
