# Thumb cache writeback at earlier exits

The opt-in bounded Thumb path now publishes only the cached fields written
before each callback or return. This is valid for its acyclic instruction
sequence: internal forward and backward targets are disabled. Reloads retain
the complete final cache layout so callback changes to later operands remain
visible. ARM lowering retains its existing behavior. Caches beyond 64 fields
fall back to conservative publication. No guest instruction, budget, interrupt,
callback, mapping check or scheduling step is removed.

All 179 compiler tests pass, including the existing full-state callback,
register-transfer and long-call boundary matrices. All 1,152 native call cases
and 2,880 native memory-fault cases pass. Fresh normal control/candidate and
checked Sky Force replays, both established Snakes routes, and normal/checked
moving-and-firing Sky Force replays match native images, records and PCM exactly.
The combat reference contains 240 images and 2,375,493 stereo PCM frames.
Selected checked invocations force memory callbacks; normal replays and fault
probes independently cover direct paths.

This archive was built from 3e088bf9c plus the recorded two-file source patch.
The intervening combat-reference commit changes only input and evidence.
The option remains disabled by default and unserved. Speed measurement follows
the separate frozen-V5 ROM-policy panel, with no overlapping owned build,
correctness job or profiler. The realtime target remains open.

## Four-route speed screen

All eight preplanned serial observations are retained.

| Route | V5 (s) | V6 (s) | Throughput change | V6 realtime ratio |
| --- | ---: | ---: | ---: | ---: |
| sky | 11.54460 | 11.45140 | +0.81% | 0.524x |
| combat | 10.89030 | 10.74080 | +1.39% | 0.559x |
| standard | 10.23610 | 12.02010 | -14.84% | 1.497x |
| long | 9.81422 | 10.07990 | -2.64% | 1.786x |

The small Sky Force differences and slower Snakes observations do not support
promotion. Each route has one pair; this cannot separate a stable feature cost
from host variability. Standard Snakes has the largest adverse observation.
The option remains unserved. The next store-continuation candidate will use V5
as its source baseline, leaving this unproven prefix-publication stage out.
A fresh V6 CPU profile follows the screen as diagnosis, not a speed result.

An initial watcher accidentally waited on its own completion marker. It was
stopped while idle, before any observation started. The marker was corrected,
the original sample plan was retained, and the screen then ran to completion.
The original script and correction record are retained in scratch evidence.
