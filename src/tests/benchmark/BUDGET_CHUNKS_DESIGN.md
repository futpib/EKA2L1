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
Other compiler policies emit their existing checks unchanged. Policy 6 is
opt-in; neither ordinary defaults nor the LAN policy change.

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
