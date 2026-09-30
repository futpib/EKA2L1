# Current custom IR versus reusing Dynarmic

Decision after the opt-in IR experiments: retain the existing precise backend
and custom snapshot machinery for current experiments. Reusing Dynarmic's
semantic frontend remains a viable separate prototype, but its existing passes
cannot be applied unchanged under our fault/budget contract. Do not commit to a
full backend rewrite on the restricted kernel timing alone.

This is a source-level review, not a new performance experiment. Inspected local
Dynarmic revision: `2375a96d2848401986ff65c3e5c37aeca80253f5`.

## What we can reuse

The local A32 frontend already expresses registers, flags and operations in a
typed IR. `src/external/dynarmic/src/dynarmic/frontend/A32/translate/a32_translate.h`
exposes both block and single-instruction translation. The latter can provide
instruction boundaries for building our PC/count/snapshot metadata, initially
for unconditional operations. It does not solve conditional execution: its ARM
implementation in `translate_arm.cpp` explicitly has TODOs for condition
handling and feedback. The block frontend has a different condition contract;
neither should be assumed interchangeable with our existing region compiler.

The existing `compiler_probe.cpp` and `ir_probe.py` already lower restricted
unconditional kernels. `COMPILER_OPTIONS_RESULTS.md` records that their apparent
math gain shrank after matching budget semantics, with faults/code writes still
not equivalent. Those results do not select a production backend.

## Contracts that need an adapter

- **Faulting reads are observable even if their value dies.** In the local
  `ir/opt/dead_code_elimination_pass.cpp`, an instruction without uses or side
  effects is invalidated. `ir/microinstruction.cpp::MayHaveSideEffects()` includes
  ordinary memory writes but not ordinary reads; `CausesCPUException()` enumerates
  explicit exception operations, not ordinary memory reads. Thus a dead ordinary
  load can be removed by these passes. Our current `region_ir` retains reads,
  writes and guards as ordered effects. Any Dynarmic adapter must preserve that
  contract or prove an access non-faulting and otherwise unobservable first.
  This is a difference in required semantics, not a claim of a Dynarmic bug.
- **Intermediate architectural state must remain reconstructible.**
  `ir/opt/a32_get_set_elimination_pass.cpp::RegisterPass()` invalidates an earlier
  set when a later set supersedes it; ordinary memory operations do not trigger
  its core-register barrier. Flags have their own elimination pass. That is not
  sufficient to preserve our callback-visible intermediate state. Snapshot
  values must be actual optimization roots/uses before elimination occurs.
- **A vector of external IR pointers is not enough.** `Inst::SetArg()` maintains
  use counts and `DeadCodeElimination` tests those counts. `IdentityRemovalPass`
  walks instruction arguments, resolves identities and erases identity/void
  nodes. An external snapshot map must participate in those rewrites and
  liveness, or import into our own stable value graph before applying them.
- **Block cycle counts are not precise partial budgets.** `IR::Block` holds a
  cycle count and terminal, while our fallback must stop at every required guest
  instruction and publish the corresponding state. We still need entry proofs,
  exact remainders, branch/interrupt guards and callback/code-write exits.

## Smallest useful next backend comparison

Translate a restricted unconditional mixed integer/load/store region one guest
instruction at a time, import the unoptimized typed operations into the current
ordered-effect/snapshot graph, and reuse our existing WASM guards and precise
fallback. Reject unsupported operations before effects. This reuses ARM
semantics while initially avoiding upstream optimization passes with incompatible
liveness assumptions. It is a proposal, not an implemented or measured backend.

Compare that adapter against the custom decoder with identical memory, budget,
exit, condition and code-validation behavior. Include dead load faults, overwritten
register values, swaps, callbacks that remap memory, self-modifying aliases and
short budgets. Only then compare startup/compiler size and whole-game throughput.
A native translation microbenchmark or a smaller generated kernel does not
establish browser integration cost or gameplay speed.

The current write/budget-proof combination deliberately reuses the existing
instruction emitter. Its independent measurements are in COMBINED_PROOFS_RESULTS.md.
