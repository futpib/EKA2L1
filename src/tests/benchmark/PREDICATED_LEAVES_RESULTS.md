# Conditional integer leaf fusion

This opt-in experiment uses the original instruction emitter (policy 7) to inline straight-line leaf functions containing conditional integer operations. The default remains disabled. Source window, leaf length, inline-site count, runner cap and guest scheduling are unchanged. No timing claim or deployment.

The exit census motivated this change: two six-instruction load/compare/conditional-MOV leaves account for 28.6% of sampled direct-call exits in the longer-snake window. Raising the 16-instruction leaf bound cannot make these leaves eligible. No guest address or captured opcode sequence is special-cased by the implementation.

## Semantics and costs

The existing integer lowering evaluates each predicate against the flags at that instruction. False predicates still consume their guest instruction budget. Inlined calls preserve real guest PC/LR values, ordered memory effects, callback-visible state, short-budget fallback and exact primary/dependency code validation. Conditional memory instructions, branches, status transfers, reserved encodings and LR/SP/PC operands remain outside the extension. Only an unconditional BX LR terminates an eligible leaf.

Fusion can remove caller/callee/return dispatcher boundaries and keep guest registers in locals across them. It can also increase generated code and dependency snapshots, and may move work into compilation and validation. Removing boundaries alone is not a speed result. Dedicated diagnostic runs will measure actual exit counts and resource changes separately from acceptance timings.

## Verification status

The focused interpreter matrix passes 29,120 comparisons across all fourteen conditions, all sixteen NZCV combinations, reads/stores, missing mappings, physical code aliases and partial/full budgets. Tests also verify rejection with the option disabled, rejection under the unrelated IR policy and exclusion of unsupported forms. Browser frontend configuration checks pass.

Acceptance passes: all 168 compiler tests, 32 native tests, frontend checks and 38,272 explicitly selected native fault comparisons. Both option-off/on checked replays exactly match 1,600 native images, guest records and 4,656,051 stereo PCM frames. The enabled-mode 360-image longer route also matches native. The new fault matrix includes 5,376 conditional-call cases in addition to the existing 13,760 cases per mode. Missing and wrong option markers are verified to fail. Probe and browser reports must verify the requested option; a claimed mode without its matching marker is rejected.

The immutable archive and its source patch are under `/home/claude/.scratch/eka-benchmark/predicated-leaf-candidate`. Evidence is recorded in PREDICATED_LEAVES_EVIDENCE.json. Future timing compares enabled/disabled execution in that same binary and the untouched post-merge baseline, with every sample and identical guest work retained.
