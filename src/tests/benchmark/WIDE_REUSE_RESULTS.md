# Adjacent long multiply result reuse: rejected

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The controlled rerun retains all eight valid observations in ABBA then BAAB
order. Its mean is -0.47% CPU and -0.30% wall throughput, with 0.072% fewer
native instructions. Three CPU pairs are slightly faster (+0.03%, +0.83%,
+0.28%); the remaining pair is -2.95%. That pair contains the valid candidate
whose measured window overlapped an unrelated `cargo test` process. Its
14.926696985-second CPU observation remains included; no rule was added to
discard it afterward. Three subsequent control attempts failed the existing
clock rule and remain recorded separately. The phase restored host settings,
waited for the host to settle, and resumed the unfinished observation.

The overlap does not prove how much interference occurred, but it limits any
interpretation of the small pooled effect. No useful gain or intrinsic
regression is established; the original large panel swings likewise do not
repeat here. See [the overlap and restoration evidence](CONTROLLED_REASSESSMENT.md).
These are the archived compiler/configuration results, not a measurement of
this change applied to today's direct-memory defaults.

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
