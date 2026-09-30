# Matched application binary: no IR promotion

> Correction (2026-09-30): standalone per-policy fault coverage is superseded by
> [the explicit-policy audit](FAULT_POLICY_AUDIT_RESULTS.md). Original raw results remain below.

The compiler now accepts an explicit translation-time policy: configured,
original emission, inline IR segments, or outlined IR segments. Browser research
configuration is restricted to before initialization and rejects unavailable
compiled features. Profile/replay tools record the policy, and serial_variants.py
accepts repeated `--ir-mode NAME=0/1/2` arguments. Ordinary runs preserve their
compile-time defaults. No policy checks are added to generated guest instructions.

| Policy | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Original emission, mode 0 | 13.5478 / 13.4976 | 13.52270 |
| Inline IR, mode 1 | 13.9982 / 13.9151 | 13.95665 |
| Outlined IR, mode 2 | 13.7211 / 15.3257 | 14.52340 |
| Existing served build | 15.0646 / 13.5367 | 14.30065 |

Order: outline/plain/inline/served/served/inline/plain/outline. The first three
policies use identical application JS/WASM bytes, selecting their generated-code
route before startup. Inline IR has 3.1% lower mean throughput than original;
outlined IR has 6.9% lower throughput. Neither is promoted. All eight runs,
including both slow observations, remain. This controls the surrounding
application binary, not browser JIT placement/state or host variation. The
sample does not establish a precise causal regression estimate. It does not
support blaming all previous IR regressions on separate application builds.

The served-build control is a separate binary; its variation makes a simple
comparison of pooled means unreliable here. No new LAN speedup is claimed.
The earlier outlining batch remains intact in IR_OUTLINE_RESULTS.md; its
improvement over inline IR does not repeat in this matched-binary batch.

All 143 compiler tests pass, including 61,152 exact state/memory/budget cases
across three policies. Each policy passes 7,104 rebuilt native/WASM fault cases
(21,312 total) and a fresh checked replay of 1,600 native-identical images,
guest records and 4,919,249 stereo PCM frames. Native CTest passes three targets;
frontend passes eight checks, including mode validation/default restoration
and rejecting mode changes after initialization. Both captured hot modules
for each policy byte-match their earlier respective compiler versions.
These are scoped fixture comparisons, not a claim that every possible module
has been exhaustively compared. The known native-identical replay movement
heuristic limitation remains separate from exact equality.

Timing was warmed, serial, hardware GPU, shared audio and unsampled, guest
seconds 78–96. Every run executes 3,975,618,624 instructions and 676 presentations.
No owned build/test/profiler overlaps the warmups or measurements. Source and
archive hashes were verified before testing/timing and sources rechecked when
recording results. Archive: `/home/claude/.scratch/eka-benchmark/ir-modes-candidate`.
Base 9735fb791 plus archived patch. Application WASM SHA-256:
`9262ccdaa4f772deb22c1e31f30083be24fd21d2d66dd98b1baebe8ec1b6eae6`.
IR_SAME_BINARY_EVIDENCE.json contains raw timings, hashes, comparisons and modes.
Experimental options remain OFF by default. Nothing pushed or deployed.

Next IR experiment: move pure values needed solely by fault snapshots into
those exits, extending the existing cold wide-half reconstruction. Load results
and every ordered memory effect must remain at their original positions; a
later store may have overwritten a load's source. Shared expression DAGs need
per-exit memoization to avoid exponential recipe expansion. This remains a
hypothesis requiring the same correctness and gameplay gates.
