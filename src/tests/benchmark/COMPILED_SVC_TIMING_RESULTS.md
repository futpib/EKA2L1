# Compiled syscall serial timing panel

All sixteen preplanned same-binary observations are retained, with two reversed-order pairs on each route. Normal browser, detailed counters and sampling disabled, shared audio enabled, mode 3, Thumb memory enabled, ARM exclusive enabled; ARM memory/ROM calls/ROM leaves disabled. Original execution limits retained. Correctness and diagnostic jobs were idle before the first observation.

| Route | Batch | Order (0 control, 1 candidate) | Control s | Candidate s | Throughput change | Candidate realtime |
|---|---:|---|---:|---:|---:|---:|
| sky | 0 | [0, 1] | 10.58480 | 10.39760 | +1.80% | 0.577x |
| sky | 1 | [1, 0] | 12.24250 | 10.10590 | +21.14% | 0.594x |
| combat | 0 | [1, 0] | 10.28110 | 9.61295 | +6.95% | 0.624x |
| combat | 1 | [0, 1] | 9.94267 | 10.69150 | -7.00% | 0.561x |
| standard | 0 | [0, 1] | 10.04190 | 9.95633 | +0.86% | 1.808x |
| standard | 1 | [1, 0] | 10.09820 | 10.41160 | -3.01% | 1.729x |
| long | 0 | [1, 0] | 9.72825 | 10.01880 | -2.90% | 1.797x |
| long | 1 | [0, 1] | 9.87080 | 10.16100 | -2.86% | 1.771x |

Guest instruction, presentation and guest-time totals match within every pair. Actual option readback is asserted in each run. The panel compares the compiled SVC option in one frozen V22d archive; it is not a total-change comparison with the untouched live build.

Coverage and speed are separate results. Assess all eight pairs before promotion; this panel alone does not measure total change against the untouched live archive. Preserve all observations. Live remains unchanged and nothing is pushed.

Decision: this does not support speed promotion. Combat reverses from +6.95% to -7.00%; standard Snakes also reverses, and both longer Snakes pairs lose about 2.9%. The +21.14% stationary result includes a slow 12.24-second control. All are retained. Candidate Sky Force remains 0.56–0.62x realtime, Snakes 1.73–1.81x. The option stays disabled by default. Further coverage experiments may explicitly enable it in both matching policies, without treating coverage as a demonstrated speed gain.
