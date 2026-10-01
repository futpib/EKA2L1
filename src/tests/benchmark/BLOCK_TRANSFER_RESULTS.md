# ARM block transfers and sustained live gameplay

CPU implementation: `2b8e56823`. Live presentation/evidence capture: `23cd26c63`.
All changes remain local on `wasm-port`; nothing was pushed.

LDM/STM instructions transferring at least two registers use one guard for an aligned, little-endian span wholly inside a permitted TLB page. The fast branch transfers all registers directly. Other cases retain the existing per-access helper/guard path. Stores overlapping current compiled code, including host aliases, set the region exit flag after completing the guest instruction. Budgets and writeback semantics remain unchanged. Self-review caught and fixed PC-publication state leaking from the generated fast branch into fallback helper emission.

## Correctness

- All **130 WASM tests pass**. This includes **381,024** bounded register/flag/memory comparisons, **46,080** long-multiply comparisons, helper-visible PC/CPSR tests, single/multiple-store code aliases and **48** permission/endian/alignment/page cases. The initial guard-test success label said 96; the nested loops actually execute 48 and the label is corrected in source.
- All three native CTest targets pass.
- The corrected build's checked **1,000-image** run matches native pixels, guest timestamps/instruction records and PCM/audio events exactly through **70.969054 guest seconds**. Visible canvas and shutdown checks pass.
- An early test fixture used an unaligned PC load that assembled an unmapped branch address. It was separated from unaligned data-transfer cases; aligned PC loads and the actual fallback callback state have explicit coverage.

Artifacts: `/home/claude/.scratch/eka-benchmark/transfer-full-checked`, `/home/claude/.scratch/eka-transfer-guards-tests.log`, and `eka-transfer-native-tests.log`. The archived `transfer-build` records binary hashes for the corrected implementation.

## Measurements and next target

A preliminary old/new/new/old physical-GPU no-capture batch (`transfer-timings`) measured 4.71209/4.56464 seconds before versus 4.06342/4.04700 after, per four guest seconds. This prototype timing **predates the fallback PC-publication correction**; it is not a final-build paired result.

The corrected build's actual Start/upload/input workflow (`transfer-live-120`) advances **113.974831 guest seconds in 120.467634 host seconds**, **0.946103x realtime**. Keyboard/touch, narrow layout, blur release and shutdown pass. Intermediate scene captures retain active gameplay; screenshot overhead remains in elapsed time. Several seconds around guest time 80–92 run below 0.7x. **Realtime playability remains unmet.** This 120-second route is not directly comparable to earlier 60-second averages.

Optional live sampling is enabled with `EKA2L1_LIVE_PROFILE_START_US=78000000 EKA2L1_LIVE_PROFILE_END_US=96000000 node live.ts ASSETS NEW_OUTPUT 100`. Sampling is diagnostic, not a timing control. `transfer-live-profile2` captured guest time 78.921973–96.813835 in a 28.476625-second sampling window. Guest-worker samples: generated code/callees 49.15%, compiled lookup 19.33%, byte comparison 9.73%, shared interpreter/dispatch 16.96%. Inclusive and self-time percentages must not be added as independent speedups. Graphics worker waits dominate its own span; page is 99.5% idle.

The first live sample attempt stopped because its color-count test rejected a legitimate monochrome game effect. Intermediate images are now retained without that color threshold; endpoint checks reject blank output and compare changed pixels. No emulator correctness failure was inferred from that harness rejection.

Next experiment: raise the compiled runner's block cap while retaining the exact guest budget, per-block interrupts and code validation. A longer 1,600-image native/browser replay is also running to extend coverage beyond the old 71-second endpoint. Larger compiled regions remain the architectural target if runner tuning is insufficient.
