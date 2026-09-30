# IR values across validated inline leaves

Policy 11 extends flag-aware mixed IR through the existing inline-leaf stream.
It does not discover larger callees or relax leaf validation. The original
resolver still accepts only bounded straight-line leaves with unchanged LR;
the region retains the same code dependencies and validation requirements.

The graph records BL as LR=caller PC+4, PC=callee entry and one guest instruction.
Its validated BX LR records the known ARM continuation and another instruction.
Every intervening instruction retains its real guest address for PC-relative
operands, precise snapshots and memory failures. Data-flow values can remain in
the graph across these boundaries. Ordinary branches, conditions, external
calls and reachable caller join labels remain boundaries; segments stay bounded
at 32 guest instructions.

Flattened caller/leaf instructions are not contiguous guest code. A segment
containing an inline transfer therefore uses a private original-compiler
fallback for just its first real instruction at its real PC when the budget is
too short. It then returns to the production runner, which continues the exact
remaining budget. This can increase dispatch near a deadline; it also reduces
fallback duplication. The experiment measures this combined design and cannot
attribute a gain solely to cross-call common-expression reuse.

This fallback never relabels the expanded instruction vector as contiguous ARM
bytes. Full-budget execution keeps ordered memory guards and effects. A failed
memory guard reconstructs exact registers, flags, LR, PC and completed count;
callbacks run after that exit. Execution resumed inside a leaf gets an ordinary
entry and does not assume the caller's earlier proof survived a callback.

Interpreter tests cover repeated calls in a loop, caller PC operands, register
cycles, flags, memory leaves, code-alias stores and all budgets from 0 to 79.
Production-runner native comparisons cover callee load/store failures, retry
and stop callbacks, visible LR/PC/flags, and budgets of 1, 2 and 3 instructions
that require the runner to continue after an early generated-function return.
Tests require actual inline-transfer IR selection.

Policies 0 through 10 keep their previous selection. The two captured policy-10
modules are byte-identical to their earlier archive. Full compiler/native/
frontend tests, rebuilt fault matrices, exact native image/audio replays and
serial same-binary gameplay controls remain required. Static size and
correctness results alone are insufficient for promotion.
