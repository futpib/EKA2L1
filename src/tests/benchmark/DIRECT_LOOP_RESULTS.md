# Direct structured guest loops

The ARM emitter now represents a restricted single-header guest loop with a
WASM loop and direct backedges. It avoids the generic PC-selector/br_table
redispatch. Multiple backedge targets or forward labels inside the loop body
retain the generic path. Straight-line inlined leaves remain eligible, with
caller and leaf addresses kept separate when opening/closing loop scopes.
Every original instruction-budget, interrupt, memory, code-write and callback
exit remains. The change has no game/address whitelist.

## Measurement and decision

The immediate fixed baseline and candidate both include the callback CPSR fix.
Real Snakes uses shared audio processing, physical GPU, rendering on, no capture
or profiling, and the same 78–96 guest-second window. Every run executes
3,975,618,624 instructions and 676 presentations. All runs including warmup are
serial; no owned correctness/build jobs overlap. All outliers are retained.

| Batch/order | Fixed baseline mean | Direct loops mean | Served mean |
| --- | ---: | ---: | ---: |
| A: fixed/direct/served/served/direct/fixed | 13.86220s | 13.70590s | 14.29470s |
| B: served/direct/fixed/fixed/direct/served | 15.01040s | 13.50360s | 13.52340s |

The candidate improves throughput by 1.14% and 11.16% against the immediate
fixed baseline; across the four
runs per variant, the mean fixed/direct comparison is about 6.1% favorable,
but overlapping ranges and slow controls limit precision. Retain the compiler
change locally on this repeated directional evidence, without promising a fixed
percentage or claiming the larger batch gain is typical.

Against the currently served archive, batch gains are 4.30% and only 0.15%.
The first contains a 15.1183-second served outlier. This does not establish a
convincing additional delivery benefit. No LAN deployment or new live/audio
acceptance is claimed here; further experiments still compare with the served
archive. This distinction also avoids attributing effects of the two recovered
compiler changes or the callback fix to this loop transformation.

## Correctness

All 136 WASM tests pass, including 461,776 exact bounded execution comparisons,
229,376 conditional ALU comparisons, 92,160 long-multiply comparisons, and 42
new pending/masked interrupt checks at entry, interior and self-loop backedges.
Eight added differential programs exercise forward exits, multiple backedges,
loop prologues, memory access, and shapes retaining generic dispatch.
The existing known XFAIL remains documented in the test harness.

All three native targets and seven frontend checks pass. Explicitly rebuilt
fault probes match native in four 672-case modes, including whole-region
callback state and deferred accesses. The checked 1,600-image replay matches
native pixels, guest records and 4,919,249 stereo PCM frames exactly. The separate
native-identical motion heuristic failure remains unchanged.

The exact tested archive is `/home/claude/.scratch/eka-benchmark/direct-loop-candidate`,
WASM SHA-256 `d0009a1e5d763cb0a8b55754ac0995c0da4c5360dbe0fa4c5f69a207785b5cdc`.
It was built at `c94acb1ca` plus its archived source/test patch. The same tested
fixtures were subsequently committed as `b9cfb7752`; no runtime source changed
during measurements. Every raw run, archive manifest and verification hash is
recorded in `DIRECT_LOOP_EVIDENCE.json`. Reproduce with `serial_variants.py`
and `EKA2L1_SHARED_AUDIO=1`, preserving the table's orders and archived builds.
