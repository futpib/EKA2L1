# Remaining costs under the delivered mode-3 policy

The frozen delivered archive `unsafe-default-candidate`, source `fba7e89b7`, is profiled on both current post-merge routes. Actual mode is 3, conditional integer leaves are enabled, additional leaf features are zero, and limits remain 512/16/8/512. All four diagnostic runs preserve the existing guest work. No build, test or second owned profiling job overlaps these serial runs.

The first pair uses detailed counters and 1 ms CPU sampling. These expose significant profiling-clock work; they are retained but do not drive the next selection. A second pair disables detailed counters and uses 5 ms CPU sampling. Neither pair is acceptance timing. No claim of speed improvement follows from diagnostic wall times.

| Route, counters disabled | Worker sampled seconds | Generated code inclusive | Outer execution self | Cache lookup self |
| --- | ---: | ---: | ---: | ---: |
| long | 9.656 | 56.26% | 18.44% | 11.00% |
| standard | 9.653 | 54.93% | 20.18% | 10.90% |

Percentages use the entire sampled span of the busy guest worker, including its waits. Generated code is inclusive of helpers; outer execution contains compiled dispatch and does not imply interpreter fallback. Worker samples and overlapping scoped durations are not summed. These are samples, not exact cost attribution. Detailed counters report zero byte comparisons under mode 3; this does not detect or prove the absence of guest code mutation.

The next independent test enables literal-PC veneer fusion (feature 128) in the same untouched delivered binary. Its existing mode-0 census removed about nine million region invocations on each route; the prior mixed timings are not reused as evidence for mode 3. The runtime target read, mapping/lifetime checks, permissions, faults and budgets remain unchanged. Current mode-3 acceptance must pass first.

The fixed timing plan contains 16 observations, two routes and two reversed batches: control/candidate/candidate/control, then candidate/control/control/candidate. Both policies use the identical untouched served archive, so the matching control is also the live-archive control; no duplicate baseline is presented as an independent build. All samples and passive host context will remain, without normalization. Normal live/audio and HTTPS checks are required for promotion. Guest scheduling is fixed.

The cached lookup fast-path layout is another measured-cost lead (about a tenth of worker samples); it is reserved for a separate experiment after this fixed panel. The existing continuing optimization request remains active. No deployment or push is part of this diagnostic.
