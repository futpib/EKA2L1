# Whole-region memory specialization

Rejected as a standalone optimization. One complete symmetric serial batch
averages 14.97730s candidate, 14.43950s corrected baseline,
and 14.07205s served build. Throughput changes are
-3.59% and -6.04% respectively.
Every run is retained. No confirmation, live acceptance or deployment followed.

| Order | Build | Seconds |
| --- | --- | ---: |
| 1 | fixed-1 | 13.4367 |
| 2 | region-1 | 14.1294 |
| 3 | served-1 | 14.7075 |
| 4 | served-2 | 13.4366 |
| 5 | region-2 | 15.8252 |
| 6 | fixed-2 | 15.4423 |

Hardware GPU/shared audio; guest 78–96 seconds, 3,975,618,624 instructions,
676 presentations. All owned warmups/timing were serial with no heavy test or
build overlap. This small batch does not establish statistical significance or
a cause of variation.

An affine pass tracks entry-relative pointers through moves and immediate
updates, proves every aligned same-page word-memory span before guest effects,
and retains host addresses. Read/write permissions are separate. Store spans
must not overlap the physical compiled-code interval or wrap their exclusive
end. Failed proofs call an outlined original compiler function. The successful
path retains instruction budgets and ordered accesses, with precise snapshots.
No guest-address whitelist or alias-unsafe memory reordering was introduced.
Version-tracking configuration remains on its existing path.

All 139 compiler tests, 4,272 explicitly rebuilt native/WASM fault comparisons
in ten modes, three native CTest targets and seven frontend checks pass.
Interpreter-checked replay matches all 1,600 images, guest records and
4,919,249 stereo PCM frames. The known native-identical movement heuristic
failure remains separate from exact equality. New generic fixtures cover
writeback, load-to-PC, aliases and proof failures; 30,976 state/memory/budget
comparisons are included in the compiler suite.

The captured math body shrinks from 12,584 to 4,580 WASM bytes, but its complete
module grows from 12,794 to 17,408 bytes because of the fallback. This is static
WASM size, not native code size or execution coverage, and did not predict speed.

Candidate: /home/claude/.scratch/eka-benchmark/region-memory-candidate,
WASM SHA-256 ebdefee13839de2a242b1bf4847834b937c3ed4535d29f35e87b514ead3187e4.
The corrected baseline is block-writeback-baseline; served control addv-candidate.
Both current builds contain interpreter callback CPSR and block-writeback fixes.
REGION_MEMORY_EVIDENCE.json embeds source/hash records, timings, exact comparisons
and static sizes. region_memory_experiment.patch preserves the standalone trial.
Reproduce with serial_variants.py, shared audio enabled, the recorded archive
paths in fixed/region/served order. Nothing was pushed or deployed.

The proof/outlining machinery remains in the uncommitted workspace as input to
the next typed-IR experiment; it is not accepted into the runtime merely because
the correctness checks passed. That next candidate must be measured separately.
