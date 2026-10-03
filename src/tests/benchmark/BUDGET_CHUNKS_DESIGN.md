# Original-emitter budget chunks

Experimental policy 6 combines delivered read-span proofs with budget checks at
straight-line chunk entry. It uses original instruction/value/memory lowering,
not the general IR. Chunks contain 4–32 instructions and split at branch targets,
control transfers, PC writes, unsupported forms and inlined-leaf boundaries.
Inlined leaves themselves retain original checks in this first version.

The usual first-instruction guard checks the budget and pending region exit,
then charges that instruction. If the remaining budget is shorter than the
remaining chunk, the emitted path undoes that charge, publishes exact current
state and PC, and invokes a private original compiled chunk with the precise
remaining budget. It restores the original budget afterward and returns the
callee's count plus the prefix count, bypassing stale caller writeback.

When the entry proof succeeds, subsequent chunk instructions omit only their
budget comparisons. Every count increment and applicable AOT_EXIT check remains,
as do the original interrupt/backedge checks, memory guards, callback handling
and code-alias exits. This proof cannot be entered through an interior label.
Policy 6 remains an explicit option. The normal browser launcher uses policy 17,
which extends these proofs to loop iterations as described below.

The test matrix compares original-emitter progress and state/memory, then checks
that count against the interpreter. It includes condition outcomes, loops,
permissions/endian/boundaries, pending interrupts and budgets around 32-instruction
boundaries. A 96-instruction program exercises three private callees and prefix
count restoration when a short budget ends in the second or third chunk.
The production fault matrices and native image/audio replay are also required.

This differs from the earlier whole-region budget trial: it can select parts of
loop regions while retaining original lowering. It still adds entry and private
fallback code; code growth or extra compilation may outweigh avoided checks.
No performance claim follows from static instruction or byte counts.

## Natural-loop iterations

The browser's default policy 17 extends policy 7's write proofs and
original instruction lowering with budget proofs for natural ARM loops of
4–32 contiguous instructions. Selection requires one backedge target, no entry
into the loop interior, no inlined code or IR segment, and a final branch back
to the head. Conditional exits outside the loop are allowed. Calls, indirect
transfers and unsupported shapes retain the existing lowering. No ROM address
or game-specific rule is used. Explicit policies, including 6 and 7, retain
their previous behavior; `EKA2L1_AOT_IR_MODE=7` selects the previous browser policy.

The loop head first honors a pending region exit, then checks whether the
remaining budget covers the longest iteration. This replaces the individual
budget comparisons, including the ordinary first-instruction budget check.
Each executed guest instruction still increments the count; all memory,
callback, code-write and backedge-interrupt checks remain.

An insufficient budget enters a private precise translation of the loop before
any instruction is charged. The callee receives the exact remainder; its state
and completed count are returned without stale caller writeback. A conditional
exit can return from this small callee before exhausting the remainder; the
normal runner continues at the published PC. A backedge re-enters the head's
proof, so the reservation cannot be reused across iterations. Time slices and
virtual time are unchanged.

A gameplay screen showed a Sky Force gain but a Snakes regression signal;
that tradeoff is accepted for the browser default. A narrower selection did
not establish a win for either game. See
[measurements and correctness evidence](LOOP_BUDGET_RESULTS.md).
