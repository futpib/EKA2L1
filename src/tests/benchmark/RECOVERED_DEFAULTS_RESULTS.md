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
| 3 | Sparse ROM lookup off/on in the rebuilt candidate | -0.27% | +1.81% | 16 valid, 0 invalid; Snakes 1/4 pairs faster, Sky Force 4/4 |
| 4 | Entry-only pruning versus current full pruning | -0.42% | +1.26% | 16 valid, 0 invalid; Snakes 1/4 pairs faster, Sky Force 3/4 |
| 5 | Static ARM count batching versus policy 17 | -0.34% | +0.50% | 16 valid, 1 invalid; Snakes 1/4 pairs faster, Sky Force 2/4 |
| 6 | Whole-entry budget guard with inline recovery | +0.31% | +1.01% | 16 valid, 0 invalid; Snakes 2/4 pairs faster, Sky Force 3/4 |
| 7 | Whole-entry budget guard with outlined recovery | +1.23% | +1.19% | 16 valid, 1 invalid; Snakes 4/4 pairs faster, Sky Force 2/4 |

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
observations; it is paused at the user's request. Its queued automatic restart has been
cancelled; [the saved checkpoint](CONTROLLED_SWEEP_CHECKPOINT.json) preserves
the stopping point. Complete the current promotion round and combined-default
measurement before proposing to resume the remaining historical sweep.

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
performance. Hardware-rendered controlled timing of all five candidates is complete. The first result is
ROM lookup: Snakes -0.27% CPU throughput (one of four pairs faster), Sky Force
+1.81% (all four pairs faster). Native instructions fall 0.14% and 1.60%,
respectively. This matches the accepted game tradeoff and is selected for final
combined validation, not yet a committed default. The measurement snapshot and
index show subsequent results.

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

Entry-only pruning has a favorable Sky Force point estimate, but its paired CPU
changes range from -3.47% to +3.35%, with only 0.03% fewer native instructions.
Snakes changes by -0.42% CPU and -0.12% native instructions. This result needs
confirmation on the final runtime before adoption; it is not the historical
3.14% gain, whose control lacked full pruning.

The current batching comparison does not support promotion. Sky Force CPU pairs
range from -1.84% to +3.78%; native instructions rise 0.24%. Snakes native
instructions rise 0.03%. Policy 17 stays selected. One Sky Force control failed
the predeclared interval-frequency limit and was repeated without changing the
criteria; the invalid observation remains in the evidence.

The inline whole-entry budget candidate removes 1.62% of native instructions in
Snakes and 0.23% in Sky Force, but CPU pairs are mixed in both games. It has not
earned default status. The outlined recovery alternative is selected for combined validation.

The next unbuilt candidate selects sparse ROM lookup by default for its combined
comparison. Its source and browser default expectations are prepared, including
real API readback, invalid-value rejection and post-initialization immutability
checks. These are pending validation, not an adopted or deployed runtime claim.

## Completed round and selected combination

All 14 current-runtime game comparisons are complete: 112 valid observations
and two retained clock-invalid attempts. The five follow-ups account for 80
valid and both invalid observations. Every reported selector matches its plan
(1,598 checked fields across 114 attempts). After timing, 77 live checks verify
restored CPU policies, EPP, CPU masks, platform profile and charging. Evidence
is `followups-live-restoration.json` and `observed-selector-audit.json`.

Sparse ROM lookup and outlined entry budgets are selected together. Outlining
reduces native instructions 1.23% in Snakes and 0.18% in Sky Force. Snakes CPU
pairs improve 0.45% to 1.88%; Sky Force pairs range from -2.01% to +5.71%, so its
positive average is not a repeatable individual gain claim. Static batching,
entry-only pruning and inline budget recovery remain unselected.

The revised syscall protocol and division lowering will be tested with sparse
ROM lookup and outlined budgets fixed, followed by a final adopted-artifact
comparison against the untouched baseline. The frozen `next-plan.json` has 32
observations; `next-replay-plan.json` requires six exact game replays. The
historical sweep remains paused and will not restart automatically.
