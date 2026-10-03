# RAM first-use compilation serial timing screen

All eight preplanned same-binary observations are retained, with one pair on each route. This is an exploratory screen, not promotion evidence. Normal browser, detailed counters and sampling disabled, shared audio enabled, mode 3, Thumb memory enabled, ARM exclusive enabled; ARM memory/ROM calls/ROM leaves disabled. Original execution limits retained. Correctness and diagnostic jobs were idle before the first observation.

| Route | Batch | Order (0 control, 1 candidate) | Control s | Candidate s | Throughput change | Candidate realtime |
|---|---:|---|---:|---:|---:|---:|
| sky | 0 | [0, 1] | 10.62890 | 11.47030 | -7.34% | 0.523x |
| combat | 0 | [1, 0] | 10.23500 | 11.55220 | -11.40% | 0.519x |
| standard | 0 | [0, 1] | 10.99650 | 12.47560 | -11.86% | 1.443x |
| long | 0 | [1, 0] | 10.26620 | 11.20190 | -8.35% | 1.607x |

Warmup (after the run call returns until the measured route begins; excludes initialization and initial loading, includes guest startup/hot compilation; not an isolated compiler timer):

| Route | Control s | Candidate s |
|---|---:|---:|
| sky | 48.970 | 53.995 |
| combat | 77.097 | 81.625 |
| standard | 14.823 | 20.093 |
| long | 28.386 | 34.156 |

Whole benchmark command (browser setup, initialization, guest warmup, measurement and export; not gameplay throughput):

| Route | Control s | Candidate s |
|---|---:|---:|
| sky | 65.241 | 71.101 |
| combat | 92.897 | 99.010 |
| standard | 31.776 | 38.692 |
| long | 44.255 | 51.113 |

Final runtime footprint (WASM allocator bytes, excluding browser and JIT code memory):

| Route | Control functions | Candidate functions | Control allocated bytes | Candidate allocated bytes |
|---|---:|---:|---:|---:|
| sky | 9583 | 15236 | 584663592 | 585604096 |
| combat | 10115 | 16561 | 593491584 | 594721512 |
| standard | 13060 | 24653 | 581626776 | 583990912 |
| long | 13525 | 25352 | 590581688 | 592974072 |

Guest instruction, presentation and guest-time totals match within every pair. Actual option readback is asserted in each run. The panel compares the RAM first-use compilation option (policy 0 versus 2) in one frozen V25 archive, with compiled SVC enabled in both configurations; it is not a total-change comparison with the untouched live build.

Coverage and speed are separate results. Assess all four pairs before promotion; this panel alone does not measure total change against the untouched live archive. Preserve all observations. Live remains unchanged and nothing is pushed.

All four exploratory pairs are slower with policy 2 (7.3–11.9% lower throughput), and its warmup is 4.5–5.8 seconds longer. This does not support speed promotion. Policy 2 remains opt-in as a coverage experiment; the sampled policy remains the default. These are single pairs, not stable universal regression estimates.
