# bounded ROM first-use replacement serial timing screen

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The controlled rerun covers standard Snakes (guest seconds 21-39) and
moving/firing Sky Force (42-48), with four fresh observations per variant in
ABBA then BAAB order. Snakes loses 3.84% CPU throughput while wall throughput
improves 0.65%; the metrics disagree and do not support a uniform speed claim.
Sky Force loses 8.14% CPU and 4.48% wall throughput. All four CPU pairs are
slower in each game. Retired native instructions increase by 1.71% in Snakes
and 1.18% in Sky Force. This does not support adopting the archived variant.
The stationary Sky Force and long-snake routes below are not repeated by these
two comparisons. No production setting changes.

## Original screen

All eight preplanned same-binary observations are retained, with one pair on each route. This is an exploratory screen, not promotion evidence. Normal browser, detailed counters and sampling disabled, shared audio enabled, mode 3, Thumb memory enabled, ARM exclusive enabled; ARM memory/ROM calls/ROM leaves disabled. Original execution limits retained. Correctness and diagnostic jobs were idle before the first observation.

| # | Route | Batch | Order (0 control, 1 candidate) | Control s | Candidate s | Throughput change | Candidate realtime |
|---:|---|---:|---|---:|---:|---:|---:|
| 1 | sky | 0 | [0, 1] | 11.86770 | 11.61280 | +2.19% | 0.517x |
| 2 | combat | 0 | [1, 0] | 10.42760 | 10.65200 | -2.11% | 0.563x |
| 3 | standard | 0 | [0, 1] | 12.15510 | 11.97330 | +1.52% | 1.503x |
| 4 | long | 0 | [1, 0] | 11.16690 | 11.47950 | -2.72% | 1.568x |

Warmup (after the run call returns until the measured route begins; excludes initialization and initial loading, includes guest startup/hot compilation; not an isolated compiler timer):

| # | Route | Control s | Candidate s |
|---:|---|---:|---:|
| 1 | sky | 53.236 | 64.022 |
| 2 | combat | 76.588 | 90.638 |
| 3 | standard | 19.339 | 30.625 |
| 4 | long | 31.144 | 46.954 |

Whole benchmark command (browser setup, initialization, guest warmup, measurement and export; not gameplay throughput):

| # | Route | Control s | Candidate s |
|---:|---|---:|---:|
| 1 | sky | 70.953 | 81.971 |
| 2 | combat | 92.594 | 107.776 |
| 3 | standard | 37.188 | 49.158 |
| 4 | long | 47.957 | 64.892 |

Final runtime footprint (WASM allocator bytes, excluding browser and JIT code memory; compiled_functions counts cumulative installations, including replaced functions, not live table slots):

| # | Route | Control cumulative installations | Candidate cumulative installations | Control allocated bytes | Candidate allocated bytes |
|---:|---|---:|---:|---:|---:|
| 1 | sky | 15236 | 89083 | 586226464 | 585344768 |
| 2 | combat | 16561 | 90269 | 595344200 | 594455032 |
| 3 | standard | 24653 | 103716 | 584616216 | 583569696 |
| 4 | long | 25352 | 104413 | 593598800 | 592547392 |

Guest instruction, presentation and guest-time totals match within every pair. Actual option readback is asserted in each run. The panel compares the bounded ROM first-use replacement option (policy 2 versus 3) in one frozen V26 archive, with compiled SVC enabled in both configurations; it is not a total-change comparison with the untouched live build.

Coverage and speed are separate results. Assess all four pairs before promotion; this panel alone does not measure total change against the untouched live archive. Preserve all observations. Live remains unchanged and nothing is pushed.

Decision: no speed promotion. The four single pairs alternate small gains and losses (+2.19%, -2.11%, +1.52%, -2.72%), while warmup increases by 10.8–15.8 seconds. Exactly zero measured interpretation is a coverage result; Sky Force remains 0.517–0.563x realtime. Keep the option off by default. Fresh CPU sampling follows separately before the structural dispatch candidate.
