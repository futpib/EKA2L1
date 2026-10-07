# Compiled syscall serial timing panel

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The completed controlled rerun covers standard Snakes (guest seconds 21-39)
and moving/firing Sky Force (42-48), with four fresh observations per variant
in ABBA then BAAB order. Standard Snakes loses 0.61% CPU and 0.50% wall
throughput, with three of four CPU pairs slower and 0.58% more native instructions.
Sky Force improves by 8.11% CPU and 7.58% wall throughput, with all four CPU
pairs faster (+3.90% to +16.87%) and 2.35% fewer native instructions. The wide
paired range matters; the mean is not a precise universal gain.

The separate `compiled-syscalls-long-snakes` rerun is also complete. All eight
browser runs match the original input hash and exact guest-work endpoints at
seconds 42-60. Long Snakes loses 0.58% CPU and 0.33% wall throughput, with
0.54% more native instructions; all four CPU pairs are slower (-1.01% to
-0.34%). The original roughly 2.9% wall loss shrinks substantially but the
controlled CPU results still show a small cost on both Snakes routes. Together
with the Sky Force gain, this is a worthwhile candidate for a current-default
comparison, not a gain shared by both games. The stationary Sky Force route
below is not repeated. No production setting changes as part of this reassessment.

## Original screen

All sixteen preplanned same-binary observations are retained, with two reversed-order pairs on each route. Normal browser, detailed counters and sampling disabled, shared audio enabled, mode 3, Thumb memory enabled, ARM exclusive enabled; ARM memory/ROM calls/ROM leaves disabled. Original execution limits retained. Correctness and diagnostic jobs were idle before the first observation.

| # | Route | Batch | Order (0 control, 1 candidate) | Control s | Candidate s | Throughput change | Candidate realtime |
|---:|---|---:|---|---:|---:|---:|---:|
| 1 | sky | 0 | [0, 1] | 10.58480 | 10.39760 | +1.80% | 0.577x |
| 2 | sky | 1 | [1, 0] | 12.24250 | 10.10590 | +21.14% | 0.594x |
| 3 | combat | 0 | [1, 0] | 10.28110 | 9.61295 | +6.95% | 0.624x |
| 4 | combat | 1 | [0, 1] | 9.94267 | 10.69150 | -7.00% | 0.561x |
| 5 | standard | 0 | [0, 1] | 10.04190 | 9.95633 | +0.86% | 1.808x |
| 6 | standard | 1 | [1, 0] | 10.09820 | 10.41160 | -3.01% | 1.729x |
| 7 | long | 0 | [1, 0] | 9.72825 | 10.01880 | -2.90% | 1.797x |
| 8 | long | 1 | [0, 1] | 9.87080 | 10.16100 | -2.86% | 1.771x |

Guest instruction, presentation and guest-time totals match within every pair. Actual option readback is asserted in each run. The panel compares the compiled SVC option in one frozen V22d archive; it is not a total-change comparison with the untouched live build.

Coverage and speed are separate results. Assess all eight pairs before promotion; this panel alone does not measure total change against the untouched live archive. Preserve all observations. Live remains unchanged and nothing is pushed.

Decision: this does not support speed promotion. Combat reverses from +6.95% to -7.00%; standard Snakes also reverses, and both longer Snakes pairs lose about 2.9%. The +21.14% stationary result includes a slow 12.24-second control. All are retained. Candidate Sky Force remains 0.56–0.62x realtime, Snakes 1.73–1.81x. The option stays disabled by default. Further coverage experiments may explicitly enable it in both matching policies, without treating coverage as a demonstrated speed gain.
