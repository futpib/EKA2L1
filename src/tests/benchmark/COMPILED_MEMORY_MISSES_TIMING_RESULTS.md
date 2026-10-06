# Compiled memory misses serial timing screen

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

All eight preplanned same-binary observations are retained, with one pair on each route. This is an exploratory screen, not promotion evidence. Normal browser, detailed counters and sampling disabled, shared audio enabled, mode 3, Thumb memory enabled, ARM exclusive enabled; ARM memory/ROM calls/ROM leaves disabled. Original execution limits retained. Correctness and diagnostic jobs were idle before the first observation.

| Route | Batch | Order (0 control, 1 candidate) | Control s | Candidate s | Throughput change | Candidate realtime |
|---|---:|---|---:|---:|---:|---:|
| sky | 0 | [0, 1] | 9.87547 | 10.21790 | -3.35% | 0.587x |
| combat | 0 | [1, 0] | 9.60959 | 9.90715 | -3.00% | 0.606x |
| standard | 0 | [0, 1] | 11.18160 | 10.90560 | +2.53% | 1.651x |
| long | 0 | [1, 0] | 9.82998 | 10.45290 | -5.96% | 1.722x |

Guest instruction, presentation and guest-time totals match within every pair. Actual option readback is asserted in each run. The panel compares the compiled memory-misses option in one frozen V23c archive, with compiled SVC enabled in both configurations; it is not a total-change comparison with the untouched live build.

Coverage and speed are separate results. Assess all four pairs before promotion; this panel alone does not measure total change against the untouched live archive. Preserve all observations. Live remains unchanged and nothing is pushed.

Decision: do not promote this as a speed improvement. The stationary/combat Sky Force pairs lose 3.35%/3.00%; standard Snakes gains 2.53%, while longer Snakes loses 5.96%. Each route has only one pair. Candidate execution is 0.587/0.606x realtime for Sky Force and 1.651/1.722x for Snakes. Retain the option for the explicitly selected coverage investigation, off by default.
