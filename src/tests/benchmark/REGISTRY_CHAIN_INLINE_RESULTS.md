# Keep ROM cache hits inside the compiled runner

Inlining the registry hit and its surrounding compiled lookup improves Sky
Force combat throughput by **5.42%** over the current hotpath-2/IR-17/Thumb-1
default. Both reversed-order pairs favor it. Snakes is effectively unchanged
(-0.11% from means, with opposing sub-percent pairs). The implementation is
adopted without a new runtime selector.

## Profile and emitted-code evidence

The current-default Chrome capture from the preceding cache-policy pass places
8.46% of Sky Force's guest-worker sampled span in `registry::lookup`; 7.81%
is under `execute_chain`. This is a ROM registry cost. RAM mapping/lifetime
validation remains in `validated_code_cache`.

The existing 4096-slot positive-hit test moves into the header, with container
recovery left in `lookup_uncached`. Keys, hashing, registration, replacement,
unregistration, clear and null-function behavior are unchanged. There is no
negative cache and no extra storage.

Inspection caught a problem with the initial prototype: exposing the registry
hit caused the surrounding `lookup_compiled_impl` to be outlined. It removed
one named call while leaving a handoff on the successful path. That prototype
passed both exact replays but was not timed or adopted. The final candidate
also forces the surrounding lookup inline. Its emitted WASM has no out-of-line
`lookup_compiled_impl` or ordinary registry lookup; calls to container recovery
remain inside the runner's miss branches. This is a structural check, not a
claim that every instruction in a sample bucket was removed.

The old [registry-only inline panel](REGISTRY_INLINE_RESULTS.md) was mixed on an
older runtime and is not pooled with these results. The new candidate explicitly
checks the complete successful dispatch path in the emitted current runtime.

Fresh post-change profiles contain no ordinary registry-lookup bucket. Sky
Force's remaining container fallback is 4.49% and its runner is 22.20%, now
including the inlined hits. Snakes' trusted RAM lookup remains 9.69%. Fractions
use the selected worker's span, including waits and collection margins; moved
work must not be counted as eliminated work. Both traces report no data loss.

## Unprofiled comparison

The fixed order is control/candidate/candidate/control on Snakes, then the same
on Sky Force. All eight observations are retained. Each starts a fresh Chromium
process, using hardware NVIDIA Vulkan, shared audio, capture mode 1 (readback
and hashing, no PNG compression), no sampling, no tracing and no verification.
Custom diagnostics are compiled out. No owned build, profile or correctness
job overlapped the timing panel; other host activity was not controlled.

| # | Workload | Control seconds | Candidate seconds | Mean throughput change | Paired changes |
| --- | --- | --- | --- | ---: | --- |
| 1 | Sky Force combat | 10.75490, 10.66720 | 10.15760, 10.16340 | +5.42% | +5.88%, +4.96% |
| 2 | Snakes standard | 2.25240, 2.28256 | 2.24527, 2.29479 | -0.11% | +0.32%, -0.53% |

Throughput change is `mean(control) / mean(candidate) - 1`. Sky Force means
are 10.71105 versus 10.16050 seconds for roughly six guest seconds; it remains
below realtime. Snakes means are 2.26748 versus 2.27003 for four guest seconds.
Two pairs on a shared host establish only this small screen's result.

Settings, assets, inputs, instruction totals and presentation boundaries match:

- Sky Force: guest microseconds 42,000,001–48,000,000,
  2,171,043,925 instructions and 192 presentations.
- Snakes: guest microseconds 21,000,000–25,000,000,
  644,728,231 instructions and 84 presentations.

Warmup is outside those windows and retained in the JSON. No times are pooled
with the preceding Thumb-memory or cache-policy measurements, and the gains
are not added or multiplied into a cumulative speed claim.

Baseline source is `e925842d4`. Baseline WASM SHA-256 is
`6a7d07b370cd7ce0fd08405c46f52644a3bd8bd8776b536fdd8ac6267842a578`;
candidate is `2f57ffb2f05651659669ad86d47e41c9c3e60906fb4ebc08251629a472ad7b89`.
Static WASM grows from 10,973,412 to 10,976,656 bytes (+3,244). The loader and
data/audio assets are byte-identical. This is a complete old/new binary
comparison, including any V8 code-layout effects, not an isolated call-cost
microbenchmark.

## Correctness and delivery

The rebuilt candidate passes the existing 160,000 registry lifecycle oracle
comparisons, 288 real lookup/runner publication checks, and 16 active/inactive
verifier protection checks. A focused `--registry-only` entrypoint permits
running that existing matrix without the unrelated compiler suites.

Fresh Snakes and moving/firing Sky Force replays each match all 60 reference
frames, pixels, instruction records, guest timestamps, PCM and audio events
exactly. References are the existing native-matched runs. No guest instruction
semantics, budget, memory permission or fault handling changes are introduced.
These focused checks are not presented as a full compiler/native fault-suite
rerun.

Post-change Chrome captures and real HTTPS launcher verification are retained
with the evidence. The launcher uses hotpath 2, Thumb memory 1 and IR 17, with
the candidate WASM. Both games reach active gameplay, accept keyboard input,
advance guest time and present frames without page errors or emulator aborts.
Profiles and correctness/deployment elapsed times are not speed measurements.

Commands, all reports, source patches, test logs and static inspection are in
[`REGISTRY_CHAIN_INLINE_RESULTS.json`](REGISTRY_CHAIN_INLINE_RESULTS.json).
Raw artifacts are under `/home/claude/.scratch/eka-profile-plateau/`; the
`registry-inline` directory contains the initial structural prototype and
`registry-inline-chain` the final candidate.
