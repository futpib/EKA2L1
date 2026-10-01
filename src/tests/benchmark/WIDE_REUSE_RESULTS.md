# Adjacent long multiply result reuse: rejected

A generic region compiler experiment carries the complete i64 result from an
unconditional long multiply into the immediately following accumulate when its
destination pair matches. Register halves still publish after every instruction;
all budgets and memory/fault exits remain. Alternate entries, loops, conditional
producers and intervening instructions invalidate reuse. The captured math loop
contains eligible pairs, but no guest address selects this optimization.

All 135 WASM tests pass, including 92,160 independent long-multiply comparisons
and strengthened forward-join/backedge cases. Three native targets, seven frontend
checks and a fresh checked replay of 1,600 images, guest records and 4,919,249 PCM
frames pass. The known motion heuristic failure remains. The explicitly rebuilt
probe passes all 672 fault cases; see FAULT_PROBE_REBUILD_AUDIT.md for the corrected
provenance. Those probes step instructions separately; a later expanded two-
instruction probe found a pre-existing callback-CPSR discrepancy in both baseline
and the next entry-budget experiment. This limits the earlier fault coverage.

| Batch | Committed compiler | Wide reuse | Served ADD/ADC build |
|---|---|---|---|
| A, flag/wide/served/served/wide/flag | 13.4419 / 15.2316 | 13.5590 / 13.5912 | 14.1765 / 14.2222 |
| B, served/flag/wide/wide/flag/served | 13.5096 / 13.7058 | 15.1692 / 16.4673 | 13.5578 / 13.6724 |

Batch A favors wide reuse by 5.6% versus the committed compiler, with a slow
closing control. Batch B regresses by 14.0%. This does not establish a repeatable
gain; all runs and outliers are retained. Every run executes 3,975,618,624 guest
instructions and 676 presentations in the 78–96 guest-second window, physical GPU,
shared audio, no capture or profiling, one owned browser at a time. Environment:
POST_REBOOT_RESULTS.md. No live promotion or deployment follows.

The rejected runtime source and binary are preserved under
`/home/claude/.scratch/eka-benchmark/wide-reuse-*`; independent regressions remain.
