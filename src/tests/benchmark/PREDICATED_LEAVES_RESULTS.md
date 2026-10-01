# Conditional integer leaf fusion

This opt-in experiment uses the original instruction emitter (policy 7) to inline straight-line leaf functions containing conditional integer operations. The default remains disabled. Source window, leaf length, inline-site count, runner cap and guest scheduling are unchanged. Repeated matching-control gains are measured below; normal/live and actual HTTPS delivery verification pass.

The exit census motivated this change: two six-instruction load/compare/conditional-MOV leaves account for 28.6% of sampled direct-call exits in the longer-snake window. Raising the 16-instruction leaf bound cannot make these leaves eligible. No guest address or captured opcode sequence is special-cased by the implementation.

## Semantics and costs

The existing integer lowering evaluates each predicate against the flags at that instruction. False predicates still consume their guest instruction budget. Inlined calls preserve real guest PC/LR values, ordered memory effects, callback-visible state, short-budget fallback and exact primary/dependency code validation. Conditional memory instructions, branches, status transfers, reserved encodings and LR/SP/PC operands remain outside the extension. Only an unconditional BX LR terminates an eligible leaf.

Fusion can remove caller/callee/return dispatcher boundaries and keep guest registers in locals across them. Each distinct fused callee adds an exact dependency snapshot. The existing cache extends its protected host-address interval using the minimum/maximum of primary and dependency backing spans; this can conservatively cover unrelated addresses between them. Fusion can therefore increase generated code, dependency validation and memory-guard exits, moving work into compilation and validation. Removing boundaries alone is not a speed result. Dedicated diagnostic runs will measure actual exit counts and resource changes separately from acceptance timings.

## Verification status

The focused interpreter matrix passes 29,120 comparisons across all fourteen conditions, all sixteen NZCV combinations, reads/stores, missing mappings, physical code aliases and partial/full budgets. Tests also verify rejection with the option disabled, rejection under the unrelated IR policy and exclusion of unsupported forms. Browser frontend configuration checks pass.

Acceptance passes: all 168 compiler tests, 32 native tests, frontend checks and 38,272 explicitly selected native fault comparisons. Both option-off/on checked replays exactly match 1,600 native images, guest records and 4,656,051 stereo PCM frames. The enabled-mode 360-image longer route also matches native. The new fault matrix includes 5,376 conditional-call cases in addition to the existing 13,760 cases per mode. Missing and wrong option markers are verified to fail. Probe and browser reports must verify the requested option; a claimed mode without its matching marker is rejected.

The immutable archive and its source patch are under `/home/claude/.scratch/eka-benchmark/predicated-leaf-candidate`. Evidence is recorded in PREDICATED_LEAVES_EVIDENCE.json. Timing compares enabled/disabled execution in that same binary and the untouched post-merge baseline, with every sample and identical guest work retained.

## Completed initial and reordered timing batches

All twenty-four trials use the immutable candidate archive or the untouched merged baseline. Fusion is the only changed setting between matching control/candidate runs; guest scheduling and all four execution limits remain unchanged. No owned build, test or diagnostic overlaps these serial timings. Every sample, including slow runs, is retained.

### long

| Batch / mode | First seconds | Reverse seconds | Mean seconds |
| --- | ---: | ---: | ---: |
| a / control | 14.6549 | 13.4966 | 14.0757 |
| a / candidate | 14.4124 | 11.1062 | 12.7593 |
| a / baseline | 15.6751 | 13.3453 | 14.5102 |

Batch a: throughput difference +10.32% versus matching control and +13.72% versus untouched baseline; 2/2 matching pairs favor fusion.

| b / baseline | 13.1487 | 11.8815 | 12.5151 |
| b / candidate | 11.0001 | 11.0987 | 11.0494 |
| b / control | 13.3601 | 13.8179 | 13.5890 |

Batch b: throughput difference +22.98% versus matching control and +13.26% versus untouched baseline; 2/2 matching pairs favor fusion.


### standard

| Batch / mode | First seconds | Reverse seconds | Mean seconds |
| --- | ---: | ---: | ---: |
| a / control | 13.4562 | 16.3658 | 14.9110 |
| a / candidate | 11.5099 | 11.1568 | 11.3333 |
| a / baseline | 11.7133 | 14.9486 | 13.3310 |

Batch a: throughput difference +31.57% versus matching control and +17.63% versus untouched baseline; 2/2 matching pairs favor fusion.

| b / baseline | 11.9574 | 11.8280 | 11.8927 |
| b / candidate | 12.3691 | 11.0680 | 11.7186 |
| b / control | 12.4749 | 13.2267 | 12.8508 |

Batch b: throughput difference +9.66% versus matching control and +1.49% versus untouched baseline; 2/2 matching pairs favor fusion.


These timings do not by themselves authorize promotion. The follow-up census must establish which boundaries were removed, where unsupported callees still exit, and whether broader dependency intervals add conservative exits. Correctness gates above apply to the immutable timing archive. Any promotion additionally requires normal/live/audio/actual-launcher acceptance. The current live build remains unchanged.

## Normal and live acceptance

The normal 1,600-image replay exactly matches native guest records and 4,656,051 stereo PCM frames. Both 120-second manual/automatic live launches verify conditional fusion and the original limits by API readback, sustain realtime and add zero measured audio underruns or drops. Maximum sampled lag is 47.65 / 30.35 ms. Startup recovery events remain in the raw records. The routes include the known native-matching level restart and do not establish uninterrupted gameplay. Local gesture audio, measured mute/unmute, keyboard/touch, pause/resume, layout and shutdown pass. The two-second process observer sees no foreign watched profiling/test jobs; this does not exclude every source of host contention. Frontend policy/readback and persistent-cache tests pass. Actual HTTPS upgrade verification also passes, as recorded below.

## Verified delivery, 2026-10-01

https://claude-laptop.lan:8188/ now serves the original conditional-only archive, explicitly selecting policy 7, predication 1 and limits 512/16/8/512. The later expanded-leaf and prefix experiments are absent from this archived binary. The actual trusted HTTPS page passes gesture audio, measured mute/unmute, keyboard/touch, pause/resume, mobile layout and shutdown. Downloaded JS, WASM and data hashes match the tested archive and content-versioned URLs.

An existing browser profile from the prior live version retains all 192,004,131 bytes of ROM/RPKG/game assets. Upgrade fetches the changed JS/WASM/data (11,444,842 transferred bytes), then reload and browser restart transfer zero runtime bodies and zero preload downloads. The original profile is preserved. API readback verifies conditional fusion and limits on each launch.

The first HTTPS probe ran before the restarted service listened and got connection refused; retry passed without changes. This harness startup race is retained in the evidence.

This delivery includes the previously verified upstream merge. Performance comparisons above use identical post-merge guest work; they do not quantify a speedup versus the older pre-merge live workload. Conditional fusion wins all eight matching-control adjacent pairs, but baseline timings vary and the reordered standard baseline lead is only 1.49%. The two-minute live routes include the native-matching level restart. No claim of universally optimal limits or uninterrupted gameplay follows. Local commits only; nothing pushed.
