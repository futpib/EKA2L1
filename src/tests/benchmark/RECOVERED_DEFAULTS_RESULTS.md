# Recovered optimization promotion

Recovered historical gains are being checked against runtime `4a339da9a` before
adoption. A modest Snakes regression is acceptable for a worthwhile Sky Force
gain. Historical percentages are not additive. The complete inventory and
current adoption decisions are in [the experiment index](EXPERIMENT_INDEX.md).

## Current-runtime measurements

The compiled syscall candidate is restored. Its Snakes off/on comparison is
complete; Sky Force and the total-change comparison remain pending. Both comparisons use fixed
3.6 GHz requests, measured reference-cycle frequency, reserved CPU 7 and sibling
15, hardware Chromium rendering, and ABBA followed by BAAB. Invalid runs remain
in the evidence and are repeated without changing the thresholds.

| # | Comparison | Snakes CPU throughput | Sky Force CPU throughput | Status |
| ---: | --- | --- | --- | --- |
| 1 | Compiled syscalls off/on in one frozen candidate | +0.70% | Pending | Snakes: 8 valid, 0 invalid, 3/4 pairs faster; Sky Force running |
| 2 | Untouched baseline versus compiled syscall candidate | Pending | Pending | Includes added runtime checks and code layout; timing queued |

Snakes wall throughput changes by +0.65%; native instructions change by -0.004%.
The small mixed-pair CPU gain is not a robust speedup claim.

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
  Explicit policy forwarding has since been fixed in the working tree; that
  additional validation awaits the next build. The game replays below actually
  select runtime policy 17.
- Both games' 60-frame replay images, frame records and PCM audio exactly
  match their native references. These deterministic replays use software
  rendering; hardware-rendered execution is part of the pending timing panel.
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
