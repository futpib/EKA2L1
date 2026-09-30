# Natural short and longer snake routes: timing pending

The game starts from identical installed assets and startup inputs. Enter at
18.64 guest seconds exits the introduction. The short route turns right at
18.69 seconds, avoiding the star; the long route waits 0.5 seconds to collect
it before turning right. Both select the overhead camera and subsequently move
straight. No game memory, assets, CPU registers, or guest clocks are edited.

Diagnostic exploration shows score 0 with the full short tail visible, versus
score 200 with a much longer tail extending beyond the lower screen and visible
again ahead on the wrapping surface. Both survive beyond guest second 60 without
additional pickups or apparent collisions. These visual observations establish
a length contrast; no exact segment count has been extracted from game memory.

Both input traces reproduce with ordinary execution of the exact served policy-7
archive, without diagnostic stepping. The checked endpoints retain their scores
and visibly different tails. The 360-image captures begin near guest second 42:

| Route | Capture span, guest seconds | Distinct images / guest second | Presentations / guest second | Million guest instructions / guest second |
| --- | ---: | ---: | ---: | ---: |
| Short | 17.561387 | 20.4426 | 40.8282 | 145.5737 |
| Longer | 17.953232 | 19.9964 | 39.9928 | 165.9842 |

The longer route performs more guest instruction work and has slightly lower
image cadence in these captures. Host capture times are not throughput evidence.
The fixed image counts cover different guest durations, so raw instruction totals
are not compared without dividing by guest time. Native image/audio equivalence
was not newly established for these new routes; these are served-build route
reproductions. Existing standard correctness replays remain separate evidence.

Next measure both over the identical 42–60 guest-second window, rendering enabled
on the physical GPU, capture/checking/profiling/counters disabled, one browser at
a time including warmup. Original code/compiler policy and archive are fixed.
All samples will be retained. The routes differ in lane, score, and whether the
star remains visible, so this is a natural gameplay comparison, not a perfectly
isolated causal effect of segment count. It also does not reproduce the user's
unspecified device/browser. No slowdown fix or new deployment is claimed.

Inputs: snakes-short.input and snakes-long.input. Raw exploration: growth-step-6
and growth-step-7 under /home/claude/.scratch/eka-benchmark. Raw served captures:
growth-short-loop-replay and growth-long-loop-replay. Hashes and observed rates
are in GROWTH_LENGTH_EVIDENCE.json.
