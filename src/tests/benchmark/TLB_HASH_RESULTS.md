# Folded data-TLB indexing: delivered

The launcher at https://claude-laptop.lan:8188/ selects the verified
`tlb-hash-candidate-v2` archive, compiler policy 7, eager regions 0, and TLB hash 1.
The binary default remains the original hash. Broader IR and deferred instruction
counts are not selected. Changes and evidence are committed locally; nothing pushed.

The new index `(page ^ (page >> 9)) & 511` distributes conflicts across the same
512-entry mapping cache. It has no game-address special cases. Insertion, lookup,
invalidation and generated guards use the same scheme; permission tags, code
validation, callbacks, exact budgets and guest memory effects retain their contracts.
Native 12l1r JIT instances keep their existing index. See TLB_HASH_DESIGN.md.

## Ordinary serial measurements

Each row contains a forward/reverse batch of six runs, including serial warmup.
All 24 observations are retained. Each run measures 18 guest seconds with physical
GPU rendering, without profiling, counters or interpreter checking. Original and
folded use one application binary; served is the exact previous LAN archive.
The longer route measures guest seconds 42-60, the standard route 78-96. Input,
window, policy, binary hashes, guest instruction totals and presentations are
verified within each workload.

| Scene / batch | Matching original | Folded | Previous served | Folded throughput vs served |
| --- | ---: | ---: | ---: | ---: |
| long-a | 13.30010s | 12.72115s | 13.33740s | +4.84% |
| long-b | 13.26770s | 12.22000s | 13.29060s | +8.76% |
| standard-a | 13.17525s | 12.40075s | 12.61030s | +1.69% |
| standard-b | 12.61310s | 12.37580s | 12.76090s | +3.11% |

All eight adjacent same-binary comparisons favor folded indexing. Pooled
throughput versus served improves 6.76% in the longer-snake scene and 2.40% in the
standard scene. Against matching original indexing, gains are 6.52% and 4.08%;
the latter is inflated by a slow original control. The slower closing candidate
in long batch A also remains. These are host/workload measurements, not a
universal guarantee. This reduces emulator overhead without removing the extra
guest work associated with a longer snake.

## Correctness and delivery gates

- Both 161-test compiler suites pass, including explicit folded-mode selection.
  The newer full-suite harness makes manual TLB fixtures respect that selection;
  the original default run used its preceding harness. A separately archived final
  high-address entry-proof regression also passes. Runtime binaries are identical.
- 27,520 rebuilt native fault comparisons match exactly: 13,760 for each explicitly
  selected index. The WASM probe checks actual DynCom instance selection, and a
  wrong-mode comparator request is rejected. Callback remapping is included.
- Both index modes pass checked native replay for 1600 images, guest records,
  audio events and 4,919,249 stereo PCM frames. Folded mode separately passes a
  normal unchecked replay of the same content, plus the longer route's 360-image,
  2,878,534-stereo-PCM-frame checked replay.
- All 32 native CPU tests (480 assertions) and frontend controls pass, including
  invalid and late configuration rejection.
- Both 120-second live/audio routes sustain realtime without additional gameplay
  underruns or drops. Maximum sampled lag is 37/46 ms; startup recovery events remain.
- Local and actual HTTPS launchers pass gesture audio, measured mute/unmute,
  keyboard/touch, visible softkey pause/resume, layout and shutdown. Actual policy
  selection is checked, and downloaded JS/WASM hashes match the accepted archive.

WASM SHA256: `2010a74b50b54d529b4175eb62a1374961b14086c9c81fa655c0eddfe961dcaf`

JS SHA256: `5ce9172048cc8fe85cb6ba6451f0cfaa963b87ac67edd19b602904e992fa74ec`

Application/fault binaries match across `tlb-hash-candidate` and its `v2` archive;
v2 changes the unit harness. Final extra-test artifacts are separately archived.
No old fault result is relabelled as a new-policy result.

## Separate diagnostic evidence

Folded indexing records 1,121,439 zero-progress calls versus 11,043,032 in the prior
served profile of the same longer route (89.85% fewer). Compiled dispatches fall
from 199,922,265 to 184,909,283; interpreter instructions from 35,800,043 to 5,202,580.
Both execute 2,987,830,398 guest instructions and 720 presentations. The dominant
sampled literal site 0x700002b8 disappears from the folded top-zero-return list.

These instrumented counts use different application archives. They support the
conflict diagnosis, but their wall times are not promotion evidence or a
same-binary causal estimate. The ordinary timings above provide that control.
Raw reports, provenance and every timing sample are in TLB_HASH_EVIDENCE.json.
