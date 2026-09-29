# Conditional MOV selection: no confirmed speedup

The experiment replaces conditional non-flag-setting MOV with WASM `select`
for immediate values and unshifted registers. PC destinations and flag-setting
forms retain their original paths. Budgets, faults and source-code validation
are unchanged. It builds on the recovered MOV/MVN and direct-flag-store commits.

Correctness passes: 135 WASM tests, including 57,344 additional independent
interpreter comparisons across all 14 conditions, 16 flag states and exact
budgets; 672 native/WASM fault cases; all three native targets; seven frontend
checks. A fresh checked replay matches native across 1,600 images, all guest
records and 4,919,249 stereo PCM frames. The separate motion heuristic retains
the known native-identical failure (408,561 us maximum interval).

| Batch/build | Host seconds | Mean |
|---|---|---|
| A: recovered flag-store baseline | 13.5347 / 13.5148 | 13.52475 |
| A: conditional MOV candidate | 13.3266 / 13.3756 | 13.3511 |
| A: served ADD/ADC baseline | 13.6628 / 13.4935 | 13.57815 |
| B: conditional MOV candidate | 13.4910 / 13.5609 | 13.52595 |
| B: served ADD/ADC baseline | 13.3945 / 13.3881 | 13.3913 |

Batch A runs flag/select/served/served/select/flag; B reverses the served/select
comparison as select/served/served/select. One owned browser runs at a time,
including warmup. All runs execute 3,975,618,624 instructions and 676
presentations over guest seconds 78–96, with physical GPU rendering and shared
audio processing; no capture or sampling. Environment: POST_REBOOT_RESULTS.md.

The first batch is 1.7% faster than the served build; confirmation is 1.0%
slower. Across both batches the candidate averages 13.438525 seconds versus
13.484725 for the served build, only 0.34% more throughput. Opposing batch
directions do not confirm a repeatable improvement. The candidate is not
promoted. Its source patch and binary are preserved in the scratch directory;
the independent condition/budget tests are retained in `5a1a291ef`.
