# Publish compiled flags before memory exception callbacks

A new whole-region fault probe exposed an existing callback-state bug. The old
probe performed MOVS and the memory instruction in separate `step()` calls, which
published packed CPSR between them. Running both instructions in one compiled
region left arithmetic NZCV flags current but packed CPSR stale. An exception
handler calling the core's `get_cpsr()` therefore saw the pre-MOVS flags.

The same expanded probe fails in the committed baseline and the experimental
entry-budget compiler: 568 of 672 callback-event comparisons disagree, while all
final registers and memory match. Their outputs are identical. For example,
MOVS r2,#7 should expose CPSR 0x20000010 to the following load's exception handler;
both instead expose the old 0xa0000010. The new probe uses native Dynarmic Step
for each instruction and one WASM run(2), comparing both instruction counts.

The fix publishes live NZCVT into packed CPSR before the AOT memory trampolines
call memory/exception handlers. Both uninstrumented and verifier paths use it.
The interpreter dispatch boundary also publishes after compiled execution, so a
deferred memory instruction sees the correct incoming flags when interpreted.
Mapped direct accesses retain their existing guards and handling. This is a
correctness change; no performance gain is attributed to it.

## Validation

The independently archived fixed baseline passes all 135 WASM tests, all three
native targets and seven frontend checks. Its fresh checked native replay matches
all 1,600 images, guest records and 4,919,249 stereo PCM frames through
102.484363 guest seconds / 16,263,331,210 instructions. The separate gameplay
motion heuristic retains its documented native-identical failure.

All four explicitly rebuilt 672-case fault modes match native completely:
`--extended`, `--deferred`, `--entry-budget`, `--entry-budget-deferred`.
These cover ordinary and deferred paths with separate-step and whole-region
budgets. Each deferred mode actually defers 356 cases. Final registers, counts,
callback state/order and every modified memory byte match. These are four modes
of the fixture set, not 2,688 unique instruction encodings or universal fault
coverage. New arguments are production-runner probes; the matched-reference
fixture rejects them explicitly.

Build the excluded fault targets explicitly as documented in
[FAULT_PROBE_REBUILD_AUDIT.md](FAULT_PROBE_REBUILD_AUDIT.md), then run both probe
executables with the same mode and compare using `--cases 672 --require-equal`.
Raw outputs, checked replay and the archived binaries/hashes are under
`/home/claude/.scratch/eka-benchmark/callback-cpsr-*`.
The archived standalone fixed WASM SHA-256 is
`70c2902732272884dd3c04dd56d0258f8864aba2bed4e2b517d3f76492cca86d`.

The fix is committed separately from the entry-budget optimization experiment.
It has not yet been deployed or passed the final two live/audio acceptance runs;
the launcher remains on the previously verified archive during further testing.
