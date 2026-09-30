# Folded data-TLB index: correctness accepted, timing pending

The opt-in hash folds higher virtual-page bits into the existing 512-slot cache.
It targets the measured conflicts in ZERO_LITERAL_RESULTS.md, without game-address
special cases. See TLB_HASH_DESIGN.md for the coherence and invalidation contract.
Default remains the original index; LAN is unchanged.

Validation completed:
- 161 compiler tests pass under the original configuration and 161 with the
  folded mode explicitly selected. The newer full-suite harness changes manual
  TLB fixtures to use that selection; the original run used the preceding harness
  with the unchanged default. An additional high-address entry-proof regression
  passes with the final test source and is archived separately.
- 27,520 rebuilt native fault comparisons match exactly: 13,760 per explicitly
  verified TLB mode, original-emitter policy7. A wrong-mode comparator request
  is rejected. The WASM probe also checks its actual DynCom instance selection.
- Both modes match native for all 1,600 standard images, guest records, audio
  events and 4,919,249 stereo PCM frames, with interpreter checking stride1024.
- The folded longer-snake route separately matches native for 360 images and
  2,878,534 stereo PCM frames, also with interpreter checking.
- All 32 native CPU tests (480 assertions) and frontend tests pass, including
  pre-init mode selection and late-change rejection.

Application SHA256: 2010a74b50b54d529b4175eb62a1374961b14086c9c81fa655c0eddfe961dcaf.
The application and fault binaries are identical in archives tlb-hash-candidate
and tlb-hash-candidate-v2; v2 rebuilds only the unit harness to select a whole
suite mode. The final extra test binary is in tlb-hash-extra-test-artifacts.
No prior fault result is being relabelled as a new-mode result.

Next: separate diagnostic block counts, then counter-free serial timing of both
modes in one application and the exact served archive. Extra hashing can introduce
new conflicts or cost more than it saves; correctness alone earns no promotion.

## Separate diagnostic, before timing

The folded profile records 1,121,439 zero-progress calls versus 11,043,032 in
the earlier served-build profile of the same longer route (89.85% fewer).
Compiled dispatches fall from 199,922,265 to 184,909,283; interpreter instructions
from 35,800,043 to 5,202,580. Both execute exactly 2,987,830,398 total guest
instructions and 720 presentations in guest seconds 42-60. The dominant sampled
literal site 0x700002b8 disappears from the folded profile's top zero-return sites.

These are instrumented diagnostic counts from different application archives,
not promotion timings or a same-binary causal estimate. The subsequent ordinary
runs hold the new application binary fixed while selecting index 0/1, with the
exact served archive as a separate control. No timing normalization is applied.

## Ordinary serial timing

All samples retained. Each batch runs the listed variants and then reverses
that order, including serial warmup. Instrumentation and checking are off.
Original and folded modes share one identical application binary; served uses
the exact LAN archive. Times measure 18 guest seconds with physical GPU rendering.

| Scene / batch | Original mean | Folded mean | Served mean | Folded throughput vs original | vs served |
| --- | ---: | ---: | ---: | ---: | ---: |
| long / a | 13.30010s | 12.72115s | 13.33740s | +4.55% | +4.84% |

Assessment remains pending confirmation and live acceptance; no deployment.
