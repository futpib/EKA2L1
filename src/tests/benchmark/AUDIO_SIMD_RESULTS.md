# Vectorize exact-order Speex interpolation in browser audio

Adopted by default: exact-order SIMD audio interpolation gives a consistent small Sky Force win, accepting a small Snakes cost.

The WASM Speex target overrides its single-precision interpolation product with four SIMD lanes, one per original accumulator. Input order and final scalar reduction order are preserved; no reassociation or fused multiply-add is introduced. The override is local to the Speex target, with no submodule edit or global SIMD build flag.

## Controlled runtime comparison

Control: `17e19c3d5`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 6.8488 → 6.8811 | -0.47% | -0.41% | -0.01% | 0/4 |
| 2 | Sky Force | 20.1541 → 19.5609 | +3.03% | +2.54% | -0.72% | 4/4 |

All four Sky Force pairs are favorable: +1.12%, +1.33%, +8.86% and +0.80%. The retained mean is +3.03%, but the unusually slow paired control inflates that mean; the other three pairs support a smaller improvement, not a reliable 3% isolated audio saving. No observation is discarded. Snakes loses 0.47%, with all four pairs slightly slower. The consistent Sky Force direction and smaller Snakes cost satisfy the stated tradeoff; the final combined comparison determines the round bottom line.

Snakes pairs range from -0.76% to -0.22%; candidate wall speed is **2.15× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from +0.80% to +8.86%; candidate wall speed is **0.82× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

Actual warmed V8 gameplay code replaces scalar vmulss/vaddss accumulator operations with executed vmulps/vaddps operations. Samples hit the vector multiply and add instructions. The complete resampler body remains 1,472 native bytes because LLVM unrolls the vector loop. The selected audio body is recovered; 159 of 160 selected native versions were recovered overall, with no sample errors or losses. Correctness and removal of scalar work do not alone establish a whole-game runtime gain.

7,680 scalar/SIMD float-bit comparisons pass over varied lengths, coefficient strides and alignments. All 20 actual shared-audio-driver rate/channel/PCM fixtures, including drain, stop, mix and queue checks, produce byte-identical stdout to the current scalar build. Both exact 60-frame game replays match images, guest progress, PCM and audio events.

The measured artifact is served at `https://claude-laptop.lan:8188/`; its served WASM hash matches. Both real game-picker paths pass gameplay, input and default-policy checks with NVIDIA hardware rendering, and saved gameplay screenshots were inspected. The existing non-silent browser-audio failure remains, so full live-audio E2E is not claimed. Exact PCM replays pass.

See [full observations and evidence](AUDIO_SIMD_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-hotspot-round/audio-simd`.
