# Loop-free entry-budget experiment

The candidate emits two bodies for a complete loop-free ARM region. An entry
check proves enough budget for the emitted extent and selects a body without
per-instruction budget comparisons. Short budgets retain the original precise
body. Both retain exact instruction counts, callback/exit checks, memory guards,
source validation and architectural state. Inlined callees and backward branches
are conservatively excluded. This duplicates code; fewer checks alone do not
establish faster execution.

The baseline and candidate both include the callback CPSR fix in `61d0219d1`.
The served archive is the earlier ADD/ADC build. All runs use real Snakes, shared
audio processing, physical GPU, rendering enabled, capture/profiling disabled,
and the same 78–96 guest-second window. Each executes 3,975,618,624 instructions
and 676 presentations. Runs are serial, including warmup; no owned builds or
correctness jobs overlap them. This is a shared host, and all outliers remain.

| Batch/order | Fixed baseline mean | Candidate mean | Served mean |
| --- | ---: | ---: | ---: |
| A: fixed/budget/served/served/budget/fixed | 14.00465s | 13.99000s | 14.26045s |
| B: served/budget/fixed/fixed/budget/served | 15.53475s | 14.32730s | 13.57965s |

The first batch is essentially tied against the fixed baseline (+0.10%
throughput). The second improves by 8.43%, with both fixed controls above 15.5s.
Against the served build the directions disagree: +1.93%, then -5.22%.
This does not establish a repeatable delivery improvement. The runtime change
is removed, its patch preserved, and no optimization is deployed.

## Correctness

All 135 WASM tests pass, including 455,504 bounded execution comparisons,
229,376 conditional ALU comparisons and 92,160 long-multiply comparisons.
All three native targets and seven frontend checks pass. Explicitly rebuilt
fault probes match native in all four 672-case modes (ordinary/deferred and
separate-step/whole-region). These are four modes of the same fixture set.
The checked native replay matches all 1,600 images, guest records and 4,919,249
stereo PCM frames. The native-identical motion heuristic failure remains
separate from exact equality. No new live/audio acceptance or LAN deployment
is claimed for this rejected candidate.

The archived candidate is `/home/claude/.scratch/eka-benchmark/entry-budget-extent-candidate`.
Its WASM SHA-256 is `17c7dc3e4d96a7e0ff919c0516d6e3b5610f936971dc6d5f34af51453fff2f6b`.
The saved patch applies to `61d0219d1`. Raw timing directories are
`entry-budget-timing-a` and `entry-budget-timing-b` under that scratch root.
Reproduce with `serial_variants.py ASSETS NEW_OUTPUT name=ARCHIVE ...`, setting
`EKA2L1_SHARED_AUDIO=1` and preserving each listed forward/reverse order.
See `ENTRY_BUDGET_EVIDENCE.json` for every observation and verification hash.
