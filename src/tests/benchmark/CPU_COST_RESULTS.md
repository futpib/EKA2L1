# Compiled CPU overhead reductions

Implementation: `ae7f28165`, based on live-input commit `20c282a59`. The change caches CPSR in generated region locals, caches ROM function lookup, and compares short validation tails with fixed-width loads. Callback barriers still flush/reload CPSR, and RAM entries still validate mapping and every compiled code byte. Generated names include guest PCs for profiling.

## Serial physical-GPU measurement

Four guest seconds, capture/readback and diagnostic counters disabled, rendering enabled; old/new/new/old trials on Chromium 150 with the NVIDIA GPU:

| Build | Host seconds |
| --- | --- |
| Before | 4.81693, 4.69883 |
| After | 4.46584, 4.45048 |

Means: 4.75788 versus 4.45816 seconds, **1.06723x throughput**, **0.89723x realtime**. Two trials per build on a shared host; this is not a sustained live-play result or an attribution of gains to individual changes.

The new build passes all 128 WASM tests and all native CTest targets. Its checked 85-image replay and checked 1,000-image replay match native pixels, timestamps, instruction records and audio exactly. The latter ends at 70.969054 guest seconds. The preceding live-input stage also passes its checked 1,000-image comparison.

## Next target selection

A separate sampled no-capture profile attributes 50.05% of guest-worker span to generated code and its callees, 14.97% to compiled lookup, 7.88% to exact code-byte comparison and 16.05% to InterpreterMainLoop (which includes compiled dispatch). These overlapping inclusive/function figures are not additive speedup estimates.

A diagnostic guest profile reconciles 617,185,821 guest instructions: 615,615,750 compiled and 1,570,071 interpreted (0.2544%). It records 52,483,984 compiled blocks and 929,010 runner calls. The next experiment removes repeated C++ calls in lookup/validation and initializes the stable TLB view once per runner. Remaining opcode hunting cannot explain the gap.

Artifacts under `/home/claude/.scratch/eka-benchmark`: `cpu-fast-build` (binary hashes), `cpu-fast-timings`, `cpu-fast-profile`, `cpu-fast-guest`, `cpu-fast-smoke`, `cpu-fast-full-checked`, and `live-base-full-checked`. Timing reproduction uses `profile_batch.py --compare-build live-base-build --before-aot 5 --after-aot 5 --capture-mode 2` with `EKA2L1_GPU=hardware EKA2L1_PROFILE_DETAIL=0`; fixtures were warmed and released only after correctness jobs finished.
