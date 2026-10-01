# Outlined code-cache lookup: acceptance

The opt-in experiment moves container recovery and mapping refresh out of the
frequent recent-entry path. Full key/live checks, mapping-source and generation
identity, exact primary/dependency bytes and terminal rejection remain. See
LOOKUP_OUTLINE_DESIGN.md for the design and static-layout limits. Code-version
and lifecycle alternatives remain off. The LAN build is unchanged.

All 162 WASM compiler tests pass. The new focused cache regression exercises both
lookup layouts, including rejection without resurrection, host writes, remaps,
dependencies, collisions, address spaces and legacy generations. All 32 native
CPU tests pass (531 assertions), with existing cache regressions expanded to both
layouts. Frontend controls reject invalid and post-initialization selection.

Both rebuilt native fault matrices match exactly: 13,760 cases per explicitly
selected lookup mode, 27,520 total. Every result verifies lookup, scanner, TLB and
compiler policy markers. A deliberately wrong lookup marker request is rejected.
Both layouts match native for all 1,600 standard images, guest records and
4,919,249 stereo PCM frames. The outlined layout also matches the longer route's
360 images, guest records and audio. These passes cover the recorded cases;
they are not a proof of all emulator behavior.

Implementation: 2fb326960. Archive: lookup-outline-candidate. Its binary and source
hashes are rechecked before this report. Logs, markers, exact comparisons and
function-size evidence are in LOOKUP_OUTLINE_EVIDENCE.json. Ordinary gameplay measurements follow. The experiment remains opt-in and
unserved until repeatable gains and delivery checks justify promotion.

## Gameplay measurements

All runs execute serially, including warmup, in fresh browsers. Original and
outlined lookups share one application binary; served is the exact live archive.
Each run covers the same 18 guest seconds for its scene, without profiling
counters. Each batch runs its listed order followed by its reverse. All samples
and slow outliers are retained. Requested scanner, TLB and compiler modes, guest
instruction totals and presentations are checked.

| Scene / batch | Original seconds | Outlined seconds | Served seconds | Gain vs original | Gain vs served |
| --- | ---: | ---: | ---: | ---: | ---: |
| long-a | 12.20305 | 11.75110 | 12.00495 | +3.85% | +2.16% |
| long-b | 12.20110 | 12.23280 | 12.18545 | -0.26% | -0.39% |

Batch A order: original, outlined, served, served, outlined, original.
Batch B order: outlined, served, original, original, served, outlined.

No promotion or deployment is implied by this timing record. See the raw rows
for individual samples and the acceptance section for correctness scope.

## Decision

Do not promote. The first longer-route batch favors the candidate by 2.16%
against the exact served archive and 3.85% against matching original lookup.
The reordered confirmation gives -0.39% and -0.26%, respectively. The closing
candidate is slower; it remains in the record without normalization or exclusion.
This does not establish a repeatable gameplay gain. Standard-scene and delivery
checks were not started. The implementation remains opt-in and the LAN stays
on the verified grouped-scanner archive.
