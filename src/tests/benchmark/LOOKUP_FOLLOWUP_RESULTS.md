# Lookup and validation follow-up

**No new speedup was established.** Three correctness-passing experiments are
removed from production; their patches and all timing observations are retained.

Baseline: `6d8fb3a10`, with runner specialization already graduated. The LAN
launcher remains pinned to that archive while candidates are evaluated.

## Fresh profile

The exact baseline archive was profiled over Snakes guest seconds 78–96 on the
physical NVIDIA GPU, rendering on, capture and detailed counters off, at 5 ms
sampling. Chromium is 153.0.8010.52. The guest worker contains 13.920223 sampled
seconds: generated regions/callees 7.347209s (52.78%), cache lookup self time
1.617726s (11.62%), exact byte comparison 1.329593s (9.55%), and
InterpreterMainLoop self time 2.073155s (14.89%). The latter includes compiled
runner work, not just interpretation. These sampled times are diagnostic, not
recoverable wall-time estimates. Graphics-worker samples are 95.2% waits.

## Short snapshot comparison: rejected

The first experiment specializes exact 4-, 8-, and 16-byte comparisons at the
lookup site using fixed-width unaligned-safe loads. Other sizes retain the
out-of-line SIMD scanner. It retains every code/dependency byte check.

It passes 134 WASM tests, all 672 native/WASM fault cases, all three
native test targets, and the checked 1,600-image native comparison including
guest records, PCM and audio events through 102.484363 guest seconds.

One browser at a time, including warmup; physical GPU; rendering on;
capture/profiling/counters off; old/new/new/old over 78–96 guest seconds:

| Build | Host seconds for 18 guest seconds | Mean |
| --- | --- | --- |
| Baseline | 14.1055 / 13.7776 | 13.94155 |
| Short comparisons | 14.9076 / 13.6992 | 14.30340 |

The candidate mean is 2.6% slower and ranges overlap. It does not earn promotion.
All runs execute 3,975,200,506 guest instructions and 676 presentations.
The experiment is preserved in `short_validation_experiment.patch`; it includes
candidate-specific tests and applies to the baseline before the separately
retained test expansion. Production code has been restored. The added test
coverage mutates every byte of short snapshots with independently aligned
source/destination pointers and remains useful for the original scanner.

## Owning-core reuse: rejected

The second candidate passes the stable owning-core reference through compiled
successor lookup, instead of calling ARMul_State::parent for each RAM region.
Only the core identity is reused: address-space state, mapping generations,
code bytes, dependencies and guest permissions remain checked as before.
It does not reintroduce the rejected short-comparison change.

It passes 134 WASM tests, all 672 fault comparisons, the checked 1,600-image
native replay, three native targets and seven frontend checks. Serial
old/new/new/old observations are baseline 23.9973 / 19.6549 seconds and candidate
18.3272 / 39.0739 seconds. Means are 21.82610 and 28.70055 seconds respectively.
The first pair favors the candidate and the second reverses it substantially.
There is no dependable improvement; all observations are retained, with no
separate host-load investigation. It is removed and preserved in
`runner_owner_experiment.patch`. These changes must not be inferred to cost
31% intrinsically from this variable two-trial batch.

## Alignment-aware recent-cache hash: rejected

The original recent-cache hash preserves the low bit imposed by ARM word
alignment. Consequently, one address space's ARM entries can use only half
of the 4,096 recent slots. The candidate XORs in the guest word index as well,
while retaining the halfword/mode and address-space mixing. A full key and live
entry check still decides hits; mapping guards and exact primary/dependency
byte validation are untouched. This is not a larger cache or a successor cache.

For 4,096 consecutive word-aligned addresses, the old/new hashes occupy
2,048/4,096 slots; halfword-aligned addresses occupy 4,096 with either hash.
That static property is not a measured gameplay collision rate or speedup.
The collision regression now uses addresses 0x1000 and 0x5000, which collide
under the new hash, and still checks host mutations, mapping-source changes
and invalidation. The change has no game-name/address whitelist.

All 134 WASM tests, 672 fault cases, three native targets, seven frontend checks
and the checked 1,600-image exact replay pass. Nevertheless, the balanced
whole-game timing is baseline 13.8304 / 15.5973 seconds versus candidate
16.6362 / 16.9320 seconds. Means are 14.71385 / 16.78410 seconds. Both candidate
trials are slower than both controls. The hash is removed, with its regression
adaptation preserved in `aligned_hash_experiment.patch`.

## Decision and practical limits

**No new performance change is graduated.** These three bounded experiments
passed correctness but did not establish a speedup on real Snakes. Each uses its
own nearby old/new/new/old controls; means from different batches must not be
compared as a causal effect. Every timing run executes identical guest work;
all observations, including the large core-pointer outlier, are retained.
No separate host-load investigation or second-game test was performed.

The fresh profile still identifies generated guest code and its memory/state
machinery as the largest bucket. This pass adds negative evidence for three
small lookup/validation rearrangements; it neither establishes a browser
performance ceiling nor shows that bigger architectural work will succeed.
No new live-play speed or smoothness claim is made.

Production emulator source is restored to the verified runner-specialization
baseline. The expanded original-scanner regression checks remain: every byte
in short snapshots is mutated with independently misaligned backing/snapshot
addresses. Patches and raw evidence are retained for reproducibility. Sound
remains off; nothing is pushed.

Reproduce with `serial_build_comparison.py ASSETS BASELINE_ARCHIVE
CANDIDATE_ARCHIVE NEW_OUTPUT`, sourcing the established process-local physical-GPU
environment. Archives are `~/.scratch/eka-benchmark/runner-specialization-build`,
`short-validation-build`, `runner-owner-build`, and `aligned-hash-build`.
Each research patch applies independently to baseline `6d8fb3a10`; the short
comparison patch includes its own test changes, so do not apply it atop the
retained test expansion without resolving those duplicate edits.

Raw trial reports, asset/binary hashes, fault checks, replay comparison and unit
logs are in `LOOKUP_FOLLOWUP_EVIDENCE.json`. Full sampled profiles remain at
`~/.scratch/eka-benchmark/post-runner-profile`.

## Restored build verification

The restored production WASM, the archived runner baseline, and a fresh download
from the trusted HTTPS launcher all have SHA-256
`783d8a6b0c88a4d2599dd97c44c44a5a059cdf2b0dcc1de68d127409e5800671`.
No server restart or deployment was needed. The existing live/replay acceptance
for that identical binary remains applicable; it was not rerun or represented
as a fresh playability result. The restored fault gate passes all 672 cases,
and all three native test targets pass.

The final restored WASM unit suite passes **134 tests**, including the retained
independent-alignment/byte-mutation coverage. All three research patches were
checked against the baseline Git index before staging these final artifacts.
