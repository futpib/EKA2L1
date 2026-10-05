# ARM short-block direct memory experiment

> Historical experiment: eligible ARM short blocks now always use inline memory.
> The option and its API have been retired; see the later
> [reassessment](ARM_MEMORY_REASSESSMENT.md) and [current behavior](README.md#memory-implementations).

The opt-in shared ARM path uses the existing permission/alignment/endian TLB
lowering in bounded, register-cached short blocks. It is independent of ROM
region selection. Eligible accesses avoid callbacks; exceptional accesses keep
the original helpers and their state barriers. Page proofs are reset across
instructions and callbacks. Modes 0/1/2 retain callback stores and their write
tracking; mode 3 allows direct stores under its accepted immutable-code policy.
The flag defaults off and cannot change after runtime initialization.

The first strengthened native probe found an inherited short-block bug:
a callback-requested stop allowed one extra instruction. All 174 mismatches
among 1,008 cases reproduced with direct memory disabled. The new direct path
records callbacks and tests the full 64-bit execution count and pending
unmasked IRQ before its successor. All accesses/writeback of the current
memory instruction finish first. The existing disabled short-block path still
has the documented bug; it is not claimed corrected by this opt-in change.

Fresh acceptance passes all 180 compiler tests, including 12,846 new full-state,
RAM, budget, endian, permission and callback comparisons. Forty focused cases
cover stop, high-half nonzero execution counts, masked/unmasked IRQ and
multi-register completion. Fresh production-runner probes match native in
4,032 ARM fault cases (modes 0/3, both TLB layouts), plus the existing 1,152
Thumb call and 2,880 Thumb memory-fault cases. All three native CTest targets pass.

All seven native image/record/PCM comparisons match exactly: stationary Sky
Force control/candidate/checked, moving-and-firing Sky Force normal/checked,
and both Snakes routes. Checked invocations force callbacks; normal replay,
unit matrices and native faults separately cover direct memory.

The initial WASM build failed on a missing compiler-header include. After that
was fixed, the ARM comparison script rejected an incorrect expected count of
720; the strengthened full-run fixture actually has 1,008 cases. Recounting
the unchanged logs exposed the real 174 stop mismatches. All failure logs,
the disabled control, frozen failed archive and corrected archive are retained.

The fixed eight-observation serial screen compares the accepted V8 allocation
archive with V10b on four routes, opposite orders across routes. Both use the
same Thumb policy, mode 3, original limits, physical GPU and shared audio;
sampling, detailed counters and capture are disabled. This includes new-binary
cost. One pair per route is exploratory, not promotion evidence. Reordered
confirmation, untouched-live controls and sustained normal live/audio checks
remain necessary. No game-specific selection, scheduling or clock change.
The realtime target is still open. Nothing deployed or pushed.

## Four-route screen

All eight planned observations are retained.

| Route | V8 seconds | V10b seconds | Throughput change | V10b realtime |
| --- | ---: | ---: | ---: | ---: |
| sky | 10.81150 | 11.69710 | -7.57% | 0.513x |
| combat | 10.79320 | 9.89625 | +9.06% | 0.606x |
| standard | 10.10310 | 9.91712 | +1.88% | 1.815x |
| long | 10.20420 | 10.62710 | -3.98% | 1.694x |

The mixed single pairs do not support promotion. Sky Force remains below
realtime. Snakes retains headroom in this screen, but the longer route loses
about 4%; these pairs do not establish repeatable gains or zero loss.
The ARM path stays opt-in while a separate normal-browser CPU profile and
possible one-page register-transfer refinement are investigated. V8 remains
the performance baseline; no default, live build or scheduling change.

A separate normal-browser sampled run attributes39.09% of CPU-worker samples
to generated-code stacks and37.45% self time to the outer interpreter frame
(which still includes the inlined compiled runner). The hottest generated
ARM function accounts for12.55% self time and uses multi-register loads.
These are diagnostic sample fractions, not isolated costs or timing samples.
A one-page transfer proof is the next generic refinement to test.
