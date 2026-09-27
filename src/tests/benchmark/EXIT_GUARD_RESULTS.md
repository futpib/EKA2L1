# Redundant region exit guards

Implementation: `1c35578d1bf2417f069dd1169db09371c666100c`.

Generated ARM regions still check the exact remaining budget before every instruction. They omit an AOT_EXIT check only when the preceding straight-line instruction cannot have changed it. Memory/helper paths, region entries and branch joins retain the check. Inlined leaf instructions keep their individual guest PCs and budgets. No code-validation check was removed.

All 131 WASM tests and all three native CTest targets pass. The checked 1,600-image browser run matches native pixels, guest timestamps/instructions and PCM/events through 102.484363 guest seconds. Artifacts: `exit-extended1600/comparison.json` and `exit-build/source.json` under `/home/claude/.scratch/eka-benchmark`.

Serial physical-GPU old/new/new/old timings for the same 18 guest seconds (78–96), with readback/capture and diagnostic counters disabled:

| Build | Host seconds |
| --- | --- |
| Leaf regions | 18.1749, 18.1349 |
| Reduced exit checks | 17.3753, 17.4542 |

Mean throughput improves **1.0425x**, to **1.0336x realtime** in this fixed heavy-window replay. Sustained live play needs separate validation; the preceding leaf build averaged 1x but temporarily fell behind. All timing trials have identical guest instruction endpoints and presentation counts. Hardware: Intel Core i7-10875H, NVIDIA Quadro T1000 Max-Q, Chrome 150.0.7871.186; shared host.

## Rejected experiment

Comparing four SIMD vectors per branch retained exact equality, passed 131 WASM tests and the checked 85-image replay, but regressed: old 17.6850/17.4821 versus new 18.0528/18.1101 seconds. Reverted it. The patch is preserved as `COMPARE64_REJECTED.patch`; no cause is assigned from timing alone.

Raw measurements and exact comparison records: `EXIT_GUARD_EVIDENCE.json`.
