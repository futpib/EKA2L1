# Historical counted-kernel harnesses

These harnesses target the compiler before commit `0e354cd12` removed guest
instruction accounting. They expect instruction budgets, count-valued returns,
and the old CPU-state layout. They are not current correctness or performance
checks. Passing current progress-valued functions to them can produce misleading
results or hang a driver loop.

Use the matching archived modules, fixtures and revision from the original
experiment report. Preserve their hashes and original environment. Relative
imports point to the generic offline WASM-cost tools, which remain active and
do not imply that the emulator counts guest instructions.

The archive contains region-IR, fixed-kernel, flag, module-layout, validated
lookup, pointer-span/lifetime, division and static-Thumb experiments. Historical
reports retain the original command paths and measurements; these moved files
are for interpreting or reproducing those historical captures only.

For the current compiler use `test_aot_wasm`, the CPU fault probes, and
[`paced-gameplay.ts`](../../paced-gameplay.ts) / [`profile.ts`](../../profile.ts).
Compare final state, callbacks, memory and completed work; a compiled function's
return value now describes progress or a syscall request, not instructions.
