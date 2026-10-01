# ARM addition overflow emission

Flag-setting ARM `ADD` and `ADC` now derive signed overflow from
`(Rn ^ result) & (operand2 ^ result)`, saving one XOR in the emitted WASM
sequence while preserving the result and flags. The optimization is generic;
it does not key on a game or guest address.

## Correctness

- 134 WASM AOT tests pass.
- All 672 native/WASM fault cases match, including callback state and exact
  changed memory.
- The checked 1,600-image Snakes replay matches the native reference through
  102.484363 guest seconds and 16,263,331,210 instructions. All 1,600 images,
  guest records, 4,919,249 stereo PCM frames and audio events match exactly.
- Native CTest passes all three targets; all seven frontend smoke tests pass.
- A two-minute live browser run advances 120.565705 guest seconds in
  120.565893 host seconds (0.999998x). Keyboard/touch, changing visible
  gameplay and shutdown pass; there are no additional audio underruns or
  dropped samples during measured gameplay. Startup recovery events remain.

## Real Snakes timing

Each serial batch covers the same 78–96 guest-second window, with rendering
and shared DSP/Cubeb audio processing enabled, capture and profiling disabled,
and one browser run at a time. Every trial executes 3,975,618,624 guest
instructions and 676 presentations. Chromium 153 and the same physical GPU
were used throughout. The baseline is the graduated shared-exit build at
`8e40b5f9d`.

| Batch | Baseline seconds | Candidate seconds | Mean baseline → candidate | Throughput change |
| --- | --- | --- | --- | ---: |
| A | 15.8370 / 13.4367 | 13.7663 / 13.6365 | 14.6369 → 13.7014 | +6.8% |
| B, reversed order | 13.5548 / 13.5640 | 13.7216 / 13.9512 | 13.5594 → 13.8364 | -2.0% |
| C | 14.5997 / 13.5544 | 13.7383 / 13.7457 | 14.0771 → 13.7420 | +2.4% |
| D, reversed order | 13.5889 / 14.2771 | 13.6874 / 13.4885 | 13.9330 → 13.5880 | +2.5% |

Across all eight runs per build, mean elapsed time is 14.0516s baseline and
13.7170s candidate, or **2.4% more throughput**. Three of four batch means
favor the candidate; one reversed batch favors baseline. Individual ranges
overlap, so this is a modest measured improvement on this host, not a universal
or precisely isolated gain. No separate host-load investigation was done.

## Reproduction and artifacts

The raw serial reports and build archives are in
`~/.scratch/eka-benchmark/addv-timing-{a,b,c,d}` and
`addv-candidate` / `epilogue-candidate`. Run the checked replay with
`EKA2L1_SHARED_AUDIO=1`, `EKA2L1_BENCHMARK_AOT=5` and
`EKA2L1_AOT_VERIFY=1024`; reproduce timings with
`serial_build_comparison.py`, reversing the build arguments for batches B and
D. The deployed WASM hash and detailed evidence are recorded in
`ADD_OVERFLOW_EVIDENCE.json`.
