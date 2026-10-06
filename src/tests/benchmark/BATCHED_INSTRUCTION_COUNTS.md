# Generic instruction-count batching

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

`EKA2L1_AOT_IR_MODE=18` extends policy 17 with deferred instruction accounting
through ARM regions. It has no game, DLL, address, loop-size or minimum-length
selection rule. Budget proofs retain their existing eligibility rules; batching
also applies outside those proofs and in their precise fallback functions.
Policy 17 remains available as the direct comparison.

The compiler represents progress as a runtime base plus a compile-time offset.
An instruction advances the offset without emitting a counter update. Budget
guards and returns read the exact sum. A cold memory rejection or unsupported
instruction still rolls back the current instruction before returning.

Internal branch edges materialize their own count. A conditional branch does
not discard the compiler offset needed by its fallthrough path. Fallthrough
materializes before closing a target label, so incoming branches cannot inherit
instructions they skipped. Backedges and loop exits use the same mechanism.
Predicated instructions, flattened inline calls and sequences spanning several
budget chunks retain their exact dynamic counts. Short ARM and Thumb blocks
already use constant counts and need no dynamic-counter batching.

Budgets, interrupt checks, callback state publication, memory permissions,
code-write handling and guest scheduling are unchanged. This optimization does
not reserve an entire batch unconditionally or charge instructions past a fault.

The focused tests compare counts, all state fields, memory and helper calls with
policy 17 across two-instruction blocks, joins, loops with interior entries,
conditional exits, unsupported instructions, several budget chunks and memory
fallbacks. Existing interpreter comparisons also run policy 18 through loop,
chunk, inline-call, predication, callback, syscall and interrupt matrices.

In the captured EUser queue traversal, seven increment-by-one assignments become
one increment-by-seven on the backedge or the fallthrough path. Its cold memory
exit keeps the precise rollback. This is emitted-WASM evidence, not a count of
V8 machine instructions or a speedup claim. V8 may already simplify some of the
previous increments, so game throughput must decide adoption.

## Measured outcome

Keep policy 18 opt-in. Two serial fresh-browser panels (ABBA, then BAAB) compare
policies 17 and 18 in the same diagnostics-free WASM binary. Sampling, tracing,
verification and custom counters are off. Each game executes identical guest
instructions and presentations across all eight observations. Every observation
is retained; outside activity on the shared host is uncontrolled.

| # | Game | Policy 17 mean seconds | Policy 18 mean seconds | Throughput change | Favorable pairs |
|---|---|---:|---:|---:|---:|
| 1 | Snakes | 2.371390 | 2.357795 | +0.6% | 2/4 |
| 2 | Sky Force | 10.445490 | 11.147500 | -6.3% | 1/4 |

Throughput change is `mean(control time) / mean(candidate time) - 1` for fixed
guest work. Snakes' initial +4.8% did not repeat in the reversed-order panel.
Sky Force's initial -2.3% became a larger pooled regression. The experiment does
not meet its recorded adoption rule: a useful roughly 3% mean gain, at least
three favorable pairs, and no repeatable material regression in the other game.
Policy 17 remains the launcher default; the live service is unchanged.

This establishes that fewer emitted counter updates are insufficient evidence
of a speedup. It does not identify the machine-code reason for the regression;
V8 can optimize both forms, and this campaign did not compare their machine code.

## Verification and reproduction

The focused matrices compare full state, memory, progress and helper behavior
against policy 17 and the interpreter. They cover short blocks, predication,
internal joins, loop entries, budget boundaries, inlined calls, syscall exits,
memory callbacks, physical aliases and pending interrupts. The separate
inline-branch entrypoint passes 94,950 additional comparisons covering both
conditional and unconditional calls.

Both 60-frame game replays match the existing native-matched references exactly,
including pixels, guest records, PCM and audio events. All 2,240 native fault
cases match. The archived native Dynarmic probe uses policy marker 7; the generated
WASM probe uses 18 and asserts that deferred lowering was selected. Both markers
are checked independently; this is not a claim of equal compiler policies.
The fault probe requires diagnostics, so it is built separately from the quiet
runtime used for replays and timings.

The source snapshot starts at `5b823fb48` and contains only this experiment's
patch. Another session was removing feature gates in the shared checkout;
those edits are excluded from this build and these measurements. The implementation
is committed as `a227e2289`. Runtime SHA-256:
`5bd6f999b24129e6c204101079a871cf5ebe111c0bdd577a8ff092c88f7e68d6`.

Select batching in a rebuilt runtime with `EKA2L1_AOT_IR_MODE=18`; use 17 for the
control. Focused generated-code checks are:

```sh
node BUILD/src/tests/aot/test_aot_wasm.js --batched-counts-only
node BUILD/src/tests/aot/test_aot_wasm.js --batched-boundaries-only
node BUILD/src/tests/aot/test_aot_wasm.js --batched-inline-branches-only
```

[Raw results](BATCHED_INSTRUCTION_COUNTS_RESULTS.json) retain the source patch,
runtime hashes, test logs, fault checks, replay comparisons, all browser commands
and observations, the decision rule and emitted-code inspection. Local binaries
and browser artifacts are under
`/home/claude/.scratch/eka-batched-counts/`. The timing runner excludes builds,
tests and profilers from its own observation windows.
