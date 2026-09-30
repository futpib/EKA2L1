# Deferred chunk counts: correctness accepted, timing pending

Policy 8 defers counter updates within already-proved straight-line chunks and
reconstructs exact counts at exits. It extends policy 7 without changing integer
or memory lowering. See DEFERRED_COUNTS_DESIGN.md. This remains opt-in and unserved.

Base `a0aaebf372e4dfcb957e469a32e04c512ba46c5c` plus archived source.patch produced
`/home/claude/.scratch/eka-benchmark/deferred-counts-candidate`.
WASM SHA-256:
`dcceea4c30d544b55af11b79d403cabb342b2e5474c7afa41a84ecd6b216464b`.

All 151 compiler tests pass, including 30,720 exact policy-8 budget comparisons
and 16,896 write comparisons. New cases cover taken/untaken forward joins and
short budgets in later chunks containing proved stores. Both explicitly rebuilt
policies 7 and 8 match native in all 7,712 fault cases each (15,424 total), including
callback remapping and precise partial state. Both checked replays match native
across 1,600 images, guest records and 4,919,249 stereo PCM frames. All three native
CTest targets and nine frontend checks pass. Existing crash-harness XFAIL and
native-identical movement-heuristic limitations remain separate.

Captured busy functions defer 47 and 64 emitted counter updates. Their hot bodies
shrink from 19,088 to 18,890 and 9,431 to 9,010 bytes; complete modules shrink from
51,883 to 51,685 and 29,065 to 28,644 bytes. Selected chunk/read/write counts are
8/9/2 and 3/11/4. The policy-7 control modules are byte-identical to the previous
archive. Size reductions are not performance evidence. Native probe timings
were concurrent with correctness work and are not used for performance claims.

Serial timing will compare policies 7 and 8 within one application binary and
the exact served read-proof archive. No live acceptance or deployment yet.
DEFERRED_COUNTS_EVIDENCE.json contains source/binary hashes and all acceptance
records. Nothing pushed.

## First serial batch

Order and elapsed seconds: deferred-1 12.5772, combined-1 12.5934,
served-1 13.2085, served-2 13.2596, combined-2 12.6404, deferred-2 12.5334.
Means: deferred 12.5553s, same-binary combined 12.6169s, served 13.23405s.
That is +0.49% throughput against the matching compiler and +5.41% against
served. Both adjacent pairs favor the candidate by small amounts. This first
batch alone does not establish a reliable marginal benefit from deferred counts.

All six runs execute 3,975,618,624 instructions and 676 presentations in the same
18 guest seconds, with shared audio, hardware GPU and no sampling. No owned
heavy jobs overlap either warmup or measurement. All samples are retained.
The reordered confirmation is running; no deployment or live acceptance yet.
