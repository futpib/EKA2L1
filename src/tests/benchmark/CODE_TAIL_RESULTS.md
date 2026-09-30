# Exact code validation with an overlapping vector tail: not promoted

Current profiles identify exact code comparison as a material CPU cost. This
opt-in experiment keeps the existing cache lookup, mapping refresh and complete
byte validation. For spans of at least 16 bytes, scanner 1 finishes with a vector
load at end minus 16, instead of scalar 8/4/2/1-byte tail checks. Each load is
within the original span. The overlap repeats some comparisons; it does not omit
any byte. Short spans retain the original scanner. Scanner 0 is the original
algorithm inside the same application binary. Both pay the policy selection.

No instruction lowering, budget, callback, dependency, or invalidation contract
changes. The default remains scanner 0. The browser API accepts selection only
before initialization. Standalone probes use an explicit trailing argument and
comparison rejects missing/wrong scanner markers, avoiding environment-only
policy attribution. Native builds retain memcmp; the expanded WASM suite is the
scanner implementation test.

Correctness acceptance is complete; the completed performance batch does not justify promotion. Archive:
/home/claude/.scratch/eka-benchmark/code-tail-candidate, base 478d4414f plus patch.
WASM SHA256 de8084b1fe75453af0ef807ec5e8cc99894d0e3ede1325987716c64f0766c1c0.
No deployment or push. Do not infer a speedup from fewer tail branches.

## Correctness acceptance

All 160 WASM compiler tests pass, including both scanners with independent
alignments, every short-span byte mutation through length 80, and sizes through
512. Native cache checks pass 293 assertions in 28 test cases. Frontend checks
pass, including rejected invalid and post-initialization scanner selection.
Wrong fault scanner markers are rejected by the comparator.

The two explicitly selected scanners each pass 13,760 native fault comparisons
(27,520 total) with compiler policy 7. Each also matches all 1,600 native images
and guest records plus 4,919,249 stereo PCM frames. Replay uses the established
interpreter-check stride 1,024. An initial launch mistakenly selected stride 1;
it was stopped before frame capture and remains recorded as incomplete. The
successful retry uses the unchanged archive. No observation was dropped from
a performance batch; timing has not yet started.

## Completed serial timing

Original0 → tail1 → served → served → tail1 → original0. All windows cover
18 guest seconds, 3,975,618,624 guest instructions and 676 presentations. No
owned heavy work overlaps warmup or timing. The first two policies use the same
application binary; served is the exact existing archive.

| Policy | First seconds | Second seconds | Mean seconds |
| --- | ---: | ---: | ---: |
| Original scanner | 12.5258 | 12.4873 | 12.50655 |
| Overlapping tail | 12.4670 | 13.3050 | 12.88600 |
| Served archive | 12.5625 | 12.7404 | 12.65145 |

The first pair marginally favors the candidate; the reverse pair loses. The
candidate mean is slower than both controls. All six observations, including
the slow closing candidate, are retained. This does not establish a repeatable
speedup or a precise intrinsic regression percentage. No confirmation batch is
justified by this result. The experiment remains opt-in and unserved.
