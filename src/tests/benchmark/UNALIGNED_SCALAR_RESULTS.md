# Scalar alignment guard experiment

The rejected TLB timing verdict below is being reassessed with measured fixed
frequency and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md)
and [scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

Direct shows a small repeatable gain: 1.3% less worker CPU in Snakes and 0.8% less in Sky Force combat. TLB has no useful gain: 1.4% more CPU in Snakes and effectively flat in Sky Force. The direct half is now adopted; TLB retains its alignment checks. The original combined patch and all observations are preserved below.

[All observations and exact commands](UNALIGNED_SCALAR_RESULTS.json); [candidate patch](UNALIGNED_SCALAR.patch).

Positive CPU change means slower. Values are worker CPU seconds; pairs are forward then reversed order.

| # | Game | Backend | Baseline CPU observations | Candidate CPU observations | Mean CPU change | Pair changes |
|---|---|---|---|---|---:|---|
| 1 | Snakes | TLB | 2.046448, 2.067918 | 2.069330, 2.103735 | +1.43% | +1.12%, +1.73% |
| 2 | Snakes | Direct | 1.729012, 1.872661 | 1.703418, 1.851877 | -1.29% | -1.48%, -1.11% |
| 3 | Sky Force combat | TLB | 9.795457, 9.164200 | 9.898975, 9.093287 | +0.17% | +1.06%, -0.77% |
| 4 | Sky Force combat | Direct | 8.539326, 8.520267 | 8.430573, 8.493009 | -0.80% | -1.27%, -0.32% |

## Scope and validation

Baseline: wasm-port at 3ca13fe3a. Candidate: ordinary ARM/Thumb scalar accesses
request alignment 1 from direct_host. The direct arena range check already
covers the entire access; outside it, direct_host emits a page-end check.
TLB scalar accesses replace natural alignment with page offset <= 4096 - width.
Byte accesses omit the vacuous alignment test. Whole-span alignment checks,
exclusive instruction support, permissions, endian state, mapping publication,
code-write policy and callback handling retain their existing implementation.
Scalar fallback within a block transfer can use the less restrictive guard.

The test expectations were updated where they specifically required a helper
for any unaligned access. Memory/state comparisons remain intact. Expanded
memory tests include starts at page offsets 4093, 4094 and 4095, and direct arena
edge and cross-page loads. This does not repair pre-existing cross-page helper
semantics or introduce alignment exceptions/legacy ARM rotated-load semantics.

Validation: 167 compiler tests passed, zero failures, with existing XFAIL and
diagnostics-only skips; 816 focused ARM/Thumb TLB/direct comparisons and direct
publication/lifetime checks passed. Four 60-frame native-reference replays
passed for Snakes and Sky Force combat under both backends, including exact
images, instructions, timestamps, PCM and audio events.

Timing: two observations per build/game/backend, ABBA within each combination.
Each run uses a fresh browser with physical GPU and shared audio. Sampling,
tracing and custom diagnostics are off. No build, compiler test or correctness
replay overlaps timing. Worker scheduler runtime is the primary metric;
renderer CPU is recorded separately. No observations are discarded.
Snakes executes 644,728,231 instructions in guest time 21-25 seconds;
Sky Force combat executes 2,171,043,925 in guest time 42.000001-48 seconds.
The driver checks identical endpoints and presentation journals across all
builds and backends. Two observations only support a small directional screen.

## Reproduction

Build baseline revision `3ca13fe3a` and freeze its browser artifacts. Apply
`UNALIGNED_SCALAR.patch`, rebuild `eka2l1_wasm` and `test_aot_wasm`, and freeze
the candidate artifacts separately. Run the compiler suite and memory checks
above, then the four native-reference replays using `memory_implementations.py`.
Once correctness jobs have finished, run:

```sh
python3 src/tests/benchmark/compare_memory_builds.py BASELINE_BUILD CANDIDATE_BUILD NEW_OUTPUT \
  --snakes-assets SNAKES_ASSETS --sky-assets SKY_ASSETS --reference-root NATIVE_REFERENCES
```

The JSON retains every underlying profile command, configuration and artifact
hash. The normal build has no custom diagnostic counters. This comparison
does not deploy either frozen build to the LAN server.

## Direct-only adoption

The production change passes alignment 1 to `direct_host` for ordinary ARM and
Thumb scalar loads/stores. No extra configuration switch is added. TLB stays
the default backend and its emitted access checks are unchanged. Span and
exclusive paths retain their existing alignment requirements.

[Adoption evidence](DIRECT_UNALIGNED_ADOPTION.json) records the production
diff, frozen build hashes, commands and results:

- 816 focused ARM/Thumb state, memory, budget, crossing and alias cases, plus
  direct CPU/MMU publication and callback checks, passed. Arena-edge tests
  also assert helper use, including unaligned and cross-page arena accesses.
- Each backend is checked against the independent interpreter at its returned
  instruction count, including registers, flags and memory. An unaligned TLB
  helper can return to dispatch earlier than the direct inline path; equal
  instruction counts still require identical results between backends.
- The 21,146-case ARM short-block memory matrix and cached callback test passed.
- Snakes and Sky Force combat each passed a 60-frame direct browser replay
  against the native reference, including exact images, instruction counts,
  guest timestamps, PCM and audio events.

This adoption reruns focused correctness checks, not the timing campaign or
full compiler suite. The direct CPU gains at the top are from the preceding
combined experiment, not newly measured values for this build.
