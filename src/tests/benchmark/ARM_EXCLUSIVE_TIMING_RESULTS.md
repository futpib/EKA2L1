# ARM exclusive-operation serial timing panel

All sixteen preplanned same-binary observations are retained, with two reversed-order pairs on each route. Normal browser, detailed counters and sampling disabled, shared audio enabled, mode 3, Thumb memory enabled, ARM memory/ROM calls/ROM leaves disabled. Original execution limits retained. Correctness and diagnostic jobs were idle before the first observation.

| Route | Batch | Order (0 control, 1 candidate) | Control s | Candidate s | Throughput change | Candidate realtime |
|---|---:|---|---:|---:|---:|---:|
| sky | 0 | [0, 1] | 10.93390 | 10.63660 | +2.80% | 0.564x |
| sky | 1 | [1, 0] | 10.99320 | 11.81620 | -6.97% | 0.508x |
| combat | 0 | [1, 0] | 10.38170 | 11.13310 | -6.75% | 0.539x |
| combat | 1 | [0, 1] | 10.51360 | 10.14510 | +3.63% | 0.591x |
| standard | 0 | [0, 1] | 10.32240 | 10.08070 | +2.40% | 1.786x |
| standard | 1 | [1, 0] | 10.40420 | 10.25170 | +1.49% | 1.756x |
| long | 0 | [1, 0] | 11.01270 | 9.79957 | +12.38% | 1.837x |
| long | 1 | [0, 1] | 9.98180 | 10.00260 | -0.21% | 1.800x |

Guest instruction, presentation and guest-time totals match within every pair. Actual option readback is asserted in each run. The panel compares the exclusive option in one frozen V20e archive; it is not a total-change comparison with the untouched live build.

The reduced interpreter count does not imply a dependable speedup: stationary Sky Force reverses direction between the two pairs. Do not promote on coverage alone. Realtime remains unachieved. Preserve the opt-in implementation and tests while investigating the remaining system-call fallback; live remains unchanged and nothing is pushed.
