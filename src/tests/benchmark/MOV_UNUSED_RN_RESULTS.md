# MOV/MVN unused source-register elimination

**Follow-up:** [post-reboot combined controls](POST_REBOOT_RESULTS.md) are mixed and do not confirm a current combined speedup. The measurements below describe the earlier environment.

On 2026-09-29, the ARM AOT translator was changed not to load or cache `Rn` for data-processing MOV and MVN, where the architecture ignores that field. The candidate is generic; it has no game/address specialization. The change is small and does not alter flags, budgets, memory handling or exit behavior.

## Correctness

- 134 WASM AOT tests pass, including 453,936 exact budget/state/memory comparisons.
- Native CTest passes all three targets.
- All 672 native/WASM fault cases match, including callback state/order and exact memory bytes; raw outputs are byte-identical.
- The checked replay matches native across 1,600 images, guest records and 4,919,249 stereo PCM frames through 102.484363 guest seconds (16,263,331,210 instructions).
- Seven frontend smoke checks pass with Puppeteer pointed at installed Chromium 153 (`PUPPETEER_EXECUTABLE_PATH=/usr/bin/chromium npm test`).

## Real Snakes timing

Four serial, balanced batches measured the same heavy 18 guest-second window on the physical NVIDIA GPU. Rendering and shared audio processing were enabled; capture and profiling were disabled. All 16 trials execute 3,975,618,624 guest instructions and 676 presentations.

| Batch | Baseline seconds | Candidate seconds | Batch mean throughput change |
|---|---:|---:|---:|
| A, baseline first | 16.6588, 15.0358 | 14.2377, 13.9069 | +12.6% |
| B, candidate first | 13.8315, 15.1573 | 15.6026, 14.8550 | -5.1% |
| C, baseline first | 15.6193, 13.7100 | 15.1585, 13.6291 | +1.9% |
| D, candidate first | 15.3027, 13.8200 | 13.7933, 13.8319 | +5.4% |

Across all trials, mean elapsed time is 14.8919s baseline and 14.3769s candidate, about 3.6% higher throughput. Three of four batch means favor the candidate; one regresses, and trial ranges overlap. This is a modest, noisy result from one shared host, not a universal gain estimate.

Two two-minute live/audio runs each sustain about 1.0x. Run A had no additional gameplay underruns or drops. Run B had three additional worklet underruns and 11,776 dropped samples during continuity, with a maximum sampled queue of 6,976 samples (about 145ms). Startup catch-up also has existing underruns/drops; these trials do not show that startup issue is fixed. The live evidence supports basic playability, not perfectly uninterrupted audio.

## Decision and reproduction

Graduated as a small emitted-code optimization because the exact correctness gates pass and three of four independent timing batches improve. The result does not consistently establish the 1.25x heavy-scene milestone. Baseline and candidate WASM SHA-256 values are `e29d57a538766b7ad3440075150b67ed1a7d4831918595c3e23951f632b0d9e0` and `119aaebfd17d28f6a9707ca0ccbf9d2a2d6a325f3e5d276efb65d290adb152dc`.

Raw timing/live/replay artifacts are under `/home/claude/.scratch/eka-benchmark/mov-unused-rn-*`; the reusable candidate build is `mov-unused-rn-candidate`. Two nearby arithmetic candidates (avoiding the shifter carry load when statically unnecessary, and simplifying CMN overflow) passed correctness but did not improve repeated Snakes timing; both were reverted and their patches retained in the scratch directory.
