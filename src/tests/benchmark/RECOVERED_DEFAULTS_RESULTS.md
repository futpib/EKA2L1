# Recovered optimization promotion

Recovered historical gains are being checked against runtime `4a339da9a` before
adoption. A modest Snakes regression is acceptable for a worthwhile Sky Force
gain. Historical percentages are not additive. The complete inventory and
current adoption decisions are in [the experiment index](EXPERIMENT_INDEX.md).

## Current-runtime measurements

The compiled syscall candidate is restored. Both games' off/on comparisons are
complete, including the total change against the untouched baseline. Both use fixed
3.6 GHz requests, measured reference-cycle frequency, reserved CPU 7 and sibling
15, hardware Chromium rendering, and ABBA followed by BAAB. Invalid runs remain
in the evidence and are repeated without changing the thresholds.

| # | Comparison | Snakes CPU throughput | Sky Force CPU throughput | Status |
| ---: | --- | --- | --- | --- |
| 1 | Compiled syscalls off/on in one frozen candidate | +0.70% | +7.17% | 16 valid, 0 invalid; Snakes 3/4 pairs faster, Sky Force 4/4 |
| 2 | Untouched baseline versus compiled syscall candidate | +0.21% | +0.71% | 16 valid, 0 invalid; Snakes 1/4 pairs faster, Sky Force 3/4 |

Snakes wall throughput changes by +0.65%; native instructions change by -0.004%.
The small mixed-pair CPU gain is not a robust speedup claim. Sky Force wall
throughput improves 6.51%; native instructions fall 4.12%. Its four CPU pairs
improve 2.76% to 12.97%. These are same-binary comparisons with all other
current production settings fixed. Against the untouched baseline, Sky Force
wall throughput changes by +0.67% and native instructions by -1.96%; its CPU
pairs range from -2.69% to +2.66%. Snakes changes by +0.27% wall and +0.29%
native instructions, with CPU pairs from -0.25% to +1.19%.

The total comparison does **not** establish a clear overall speedup. The 7.17%
same-binary gain must not be reported as the benefit of adopting this change.
Added runtime work and code layout are included in the total comparison; their
individual contributions have not been isolated. Compiled syscalls remain a
candidate, and no runtime default has been committed from this promotion.
All 32 observations were valid. CPU policy, affinity groups, platform profile
and charging were restored, including a live sysfs check after the runs.
The [measurement snapshot](RECOVERED_DEFAULTS_RESULTS.json) preserves every
completed observation, frequency check, build hash and hardware-counter result.

Artifacts and frozen plans are under
`/home/claude/.scratch/eka-promote-recovered/`. The historical reassessment under
`/home/claude/.scratch/eka-controlled/` retains its original plans and valid
observations; it is paused between observations for this promotion work.

## Compiled syscall correctness

Generated ARM/Thumb SVC code publishes a pending trap and returns to the runtime.
The runtime invokes the existing kernel callback with the original cumulative
instruction count and preserves PC/mode changes, budget semantics, flags,
interrupt boundaries and exclusive reservations. There is no game or syscall
number specialization. The browser selector is `EKA2L1_COMPILED_SVC=0|1`, frozen
before initialization, with readback and post-initialization rejection checks.

Validation of the candidate:

- 168 compiler tests passed. This diagnostics-free build explicitly skips
  diagnostic-only fixtures; those skips are not coverage claims.
- 9,216 generated syscall descriptor/predicate/budget/page checks passed.
- 10,368 native DynCom versus generated WASM comparisons passed, in mutation
  modes 0 and 3. They compare every emitted field, including callback-visible
  state, PC/mode changes, stops, budgets, IRQs and both exclusive reservations.
  Test-only wrappers prove that generated functions actually execute without
  requiring production instrumentation. The restored standalone fixture used the
  translator's configured policy even though its command reported policy 17.
  Explicit policy forwarding was then fixed and all 10,368 comparisons
  passed again with policy 17 actually selected. The game replays below also
  select runtime policy 17.
- Both games' 60-frame replay images, frame records and PCM audio exactly
  match their native references. These deterministic replays use software
  rendering; all 32 timing observations used hardware rendering.
- System Chromium passed the browser API suite, including the candidate's
  default, configuration/readback, and invalid mode rejection. The initial
  attempt used Puppeteer's missing bundled browser; rerunning with
  `PUPPETEER_EXECUTABLE_PATH=/usr/bin/chromium` passed.

Run the callback comparison with:

```sh
python3 src/tests/benchmark/run_compiled_svc_matrix.py \
  build/src/tests/eka_matched_fault \
  build-wasm/src/tests/aot/eka_cpu_fault_wasm.js /tmp/eka-svc-results
```

The original evidence remains in [compiled syscall results](COMPILED_SVC_RESULTS.md)
and [the historical timing report](COMPILED_SVC_TIMING_RESULTS.md). The historical
controlled comparison recovered +8.11% Sky Force CPU throughput with a 0.61%
Snakes cost; this does not establish the gain of the current candidate.

## Other recovered candidates

Sparse ROM lookup, entry-only state pruning, generic ARM count batching, and
whole-entry/outlined budget alternatives are restored for comparison with the
current runtime. They are not adopted defaults. The rebuilt native and WASM
fixtures passed focused checks, including 320,000 sparse registry comparisons,
17,280 exact three-variant budget comparisons, and 34,560 instruction-batching
comparisons. Additional batching fixtures cover short budgets, leaves, IRQs,
code aliasing, callbacks and SVC boundaries.

All 12 selected game replays passed: the rebuilt control and each of the five
candidate settings reproduce both native references exactly across 60 frames,
frame records and PCM. Every replay verifies the actual runtime selector
readbacks, binary hashes and input hash. Evidence is in `followups-replays/`;
`followups-hashes.json`, `followups-harness-hashes.json` and
`followups-complete-source.patch` identify the frozen runtime and harness. The
initial harness launch caught a malformed destructuring parameter before any
game execution; its failure is preserved, and the corrected harness passed all
12 runs. These replays use software rendering and establish correctness, not
performance. Hardware-rendered controlled timing is running. The first completed result is
ROM lookup in Snakes: -0.27% CPU throughput, with one of four pairs faster;
Sky Force is pending. The measurement snapshot and index show subsequent results.

The five independent timing comparisons hold compiled syscalls enabled and
all other current settings fixed. Their off/on results will need a combined
comparison against the untouched baseline before default adoption, including
any runtime cost from restoring selectors or lookup alternatives. The archived
division-digit candidate is also queued: its controlled historical Snakes CPU
gain was +0.94% in all four pairs, while Sky Force's +0.67% was mixed.

## Next candidate preparation

The original SVC candidate adds a pending-trap test after every compiled region.
A revised protocol is prepared in the working tree: a trap returns the existing
zero chain-stop sentinel and publishes its logical instruction count separately.
Ordinary successful regions can then avoid the new pending-trap load/test.
Diagnostic and single-region execution must still report the logical count.
This version has not yet been built, tested or timed; the frozen timing binaries
and current measurements continue to use the original protocol.

Division lowering is also prepared behind an off-by-default compiler selector,
`EKA2L1_DIVISION_DIGITS=0|1`. Its static count update composes with policy18,
and whole-entry budget proofs can establish its budget bound. The restored
interpreter matrix covers both count policies and all three entry-budget modes.
That expanded matrix has not yet run. Builds and tests wait until the serial
benchmark queue is finished.
