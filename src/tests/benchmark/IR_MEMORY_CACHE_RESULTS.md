# Page reuse in mixed IR: correct, no promotion

This revision restores the existing emitter's function-local read/write page
proof reuse inside IR guarded-host nodes. An aligned matching page key and a
span that fits in that page use the cached host base. Misses retain the original
full proof. Read and write permissions remain separate; stores always retain
physical code-overlap and wrapping-end guards. Helpers invalidate the keys.

| Build | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Corrected default | 13.5252 / 13.5732 | 13.5492 |
| IR with page reuse | 14.2608 / 15.2612 | 14.7610 |
| Served build | 14.4971 / 15.4703 | 14.9837 |

Order: fixed/cached/served/served/cached/fixed. This candidate is 8.2% lower
throughput than corrected default and 1.5% higher than served in this batch.
It does not beat the corrected compiler, and is not promoted. All runs and
outliers are retained. The previous uncached IR ran in a separate batch;
comparing its mean directly does not isolate the causal benefit of page reuse.
No live acceptance or deployment was attempted.

Every run executes guest seconds 78–96, 3,975,618,624 instructions and 676
presentations with hardware GPU and shared audio. Warmup and timing are serial,
with no owned builds/tests/profilers overlapping. Diagnostic correctness work
was separate and its wall times are not performance measurements.

Full WASM suite: 142 passed, including 6,152 exact intermediate snapshot/cache
comparisons and 2,560 inlined-leaf cases. All 5,472 explicitly rebuilt native
fault comparisons match. Native CTest passes all three targets; frontend passes
all seven checks. Checked native replay matches all 1,600 images, guest records
and 4,919,249 stereo PCM frames exactly. The native-identical movement-heuristic
limitation remains separate from exact equality.

Archive: `/home/claude/.scratch/eka-benchmark/ir-memory-cache-candidate`.
Base `ed4b856ed` plus archived patch; source hashes checked before timing.
Main WASM SHA-256:
`e5a1b36cea7a256998f5c0c87780641c5787e7fba77217c4456e3715241ed036`.
Raw evidence and provenance: `IR_MEMORY_CACHE_EVIDENCE.json`.

The two captured busy region bodies are now 29,693 and 16,156 bytes, with 17 and
10 segments. Page hits execute fewer lookup operations, but cold lookup and
snapshot code still occupies the function alongside its original remainder.
This implementation stays within the opt-in research path; it is not a claimed
accepted speedup. Wide-value lowering and cold snapshot recipes remain future
experiments. The default compiler and served build do not enable this path.
