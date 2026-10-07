# Profile-driven dispatch optimization: stopping at diminishing returns

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The controlled RAM-hit-inlining rerun now has four observations per variant
in ABBA then BAAB order for each game. Snakes improves by 3.66% CPU and 2.77%
wall throughput, with 4.54% fewer retired native instructions; all four CPU
pairs are faster (+1.93% to +5.83%). Sky Force improves by 2.14% CPU and 2.05%
wall throughput, with 0.60% fewer native instructions and all four CPU pairs
faster (+1.00% to +4.84%). These use guest seconds 21-25 and 42-48 respectively.
The original Snakes rejection does not hold for this controlled comparison,
and the new Sky Force measurements add coverage absent from that screen. This
supports rechecking the change on today's implementation; the percentages
belong to the archived builds and cannot be added to current throughput.
The separate verifier and guard-publication comparisons are still pending.

## Original campaign

This campaign adopts one further improvement: keeping ROM registry hits and
their surrounding lookup inside the compiled runner. It improves Sky Force
combat throughput by **5.42%**, with favorable pairs in both orders. Snakes is
effectively unchanged (-0.11%). It is committed as `164173789`, verified in both
games through the actual HTTPS launcher, and deployed on `.lan`. Full evidence
is in [the registry report](REGISTRY_CHAIN_INLINE_RESULTS.md).

Three follow-ups did not justify adoption. The source is restored to that
verified ROM-inline implementation, with hotpath 2, Thumb memory 1, IR 17 and
guard publication enabled. This is a stopping point for the investigated local
dispatch changes, not evidence of a universal performance ceiling.

## RAM cache hit inlining: rejected after confirmation

The new profile puts 9.69% of Snakes' worker span in trusted RAM cache lookup.
The candidate moves its recent-entry/live/key/generation/source checks into the
runner and leaves container/mapping recovery out of line. Every mapping and
lifetime check remains. Emitted WASM confirms the ordinary trusted lookup is
inlined and the recovery function remains separate.

It passes 16,384 cache lifecycle comparisons, the existing outlined-cache
regressions, 288 real runner/publication checks, 16 verifier protection checks,
and a 60-frame Snakes replay with exact pixels, guest records, PCM and events.
Correctness does not establish performance.

The initial ABBA screen shows +3.87%, but its pairs differ (+6.89%, +1.02%).
Before starting the longer Sky Force matrix, a prospective BAAB confirmation
requires the pooled mean to retain roughly 3% and at least three of four pairs
to favor the candidate. The confirmation reduces the pooled gain to **1.51%**
and includes a reversal. No samples are discarded.

| # | Panel and order | Control seconds | Candidate seconds | Paired throughput changes |
| --- | --- | --- | --- | --- |
| 1 | Initial: control/candidate/candidate/control | 2.31375, 2.31988 | 2.16460, 2.29638 | +6.89%, +1.02% |
| 2 | Confirmation: candidate/control/control/candidate | 2.23219, 2.20852 | 2.20540, 2.27287 | +1.21%, -2.83% |

The rejected patch, binary, tests and all eight observations are retained.
Production source is restored; no Sky Force expansion or deployment follows.
This is insufficient evidence for a useful gain, not proof of intrinsic
slowdown or of zero benefit on every workload.

## Remaining runner checks: exploratory screen

The current profile has runner self samples of 15.50% in Snakes and 22.20% in
Sky Force, including the newly inlined ROM hits. Two existing selectors remove
specific work from that body:

- Verification specialization: hotpath 3 instead of 2 omits verifier-disabled
  `validation_running` checks. Reference-verifier protection remains.
- Guard publication omission: `EKA2L1_OMIT_GUARD_PUBLICATION=1` skips stores to
  code-guard interval fields unread by mode-3 generated code. Compatibility
  modes retain publication.

The predeclared screen tests each separately in the same verified binary.
Snakes order is control/verification/guards/control; Sky Force reverses the
candidate order. Each candidate has one observation per game, bracketed by two
controls. Only a candidate roughly 3% faster than the faster control in either
game, without a substantial tradeoff in the other, merits fresh reversed-order
confirmation. This screen alone cannot promote a setting.

| # | Workload | Control seconds | Verification seconds | Verification change versus controls | Guard omission seconds | Guard change versus controls |
| --- | --- | ---: | ---: | --- | ---: | --- |
| 1 | Snakes | 2.22076, 2.26201 | 2.28004 | -2.60% to -0.79% | 2.32462 | -4.47% to -2.69% |
| 2 | Sky Force combat | 10.10980, 10.29070 | 10.33290 | -2.16% to -0.41% | 10.23020 | -1.18% to +0.59% |

Neither meets the confirmation threshold. The verification observation is
slower than both controls in each game. Guard omission is slower in Snakes and
between Sky Force's controls. Both settings remain disabled. No new full replay
or deployment matrix is run for these rejected settings; the actual runner
and verifier checks on the accepted binary already cover their semantics.

## Controls, restoration and practical limits

Every timing uses a fresh Chromium process, hardware NVIDIA Vulkan, stock 5320
assets, shared audio and capture mode 1 (readback/hash without PNG compression).
Sampling, tracing and verification are off; custom diagnostics are compiled
out. No owned build, profile or correctness job overlaps a timing panel. Other
host activity is uncontrolled. Warmup is outside the window and retained,
including the 101.75-second verifier-screen Sky Force warmup.

Every Snakes observation executes the same 644,728,231 instructions and 84
presentations over guest seconds 21–25. Every Sky Force observation executes
2,171,043,925 instructions and 192 presentations between guest microseconds
42,000,001 and 48,000,000. Raw commands, settings and exact boundaries remain in
the JSON. Earlier campaigns' observations are not pooled with these timings.

The production build is rebuilt after restoration and matches the served
ROM-inline WASM byte for byte, SHA-256
`2f57ffb2f05651659669ad86d47e41c9c3e60906fb4ebc08251629a472ad7b89`.
Focused restored registry/cache/verifier/runner checks pass. The real two-game
launcher checks belong to the adoption of this identical binary; final hash
verification does not imply a new gameplay run.

The adopted gain, rejected source patch, plans and prospective decision rules,
all observations, test output and restoration hashes are in
[`PROFILE_OPTIMIZATION_PLATEAU_RESULTS.json`](PROFILE_OPTIMIZATION_PLATEAU_RESULTS.json).
Raw artifacts remain under `/home/claude/.scratch/eka-profile-plateau/`.
