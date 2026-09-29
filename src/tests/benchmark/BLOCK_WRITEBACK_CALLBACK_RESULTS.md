# Ordinary block-transfer callback writeback ordering

The extended memory-proof acceptance probe exposed an existing interpreter/native
callback difference even after the CPSR fix. If a deferred scalar access enters
an interpreter block containing a later LDM/STM with writeback, its address helper
updates the base register before the memory callback. Native and the existing
compiled block-transfer path expose the original base until the transfer ends.

The interpreter now preserves the computed start address and updated base, restores
the original base during transfers, then publishes writeback after them. This is
restricted to ordinary S=0 transfers with a nonempty register list, base other than
PC and base absent from the list. Banking/SPSR and base-in-list cases retain their
existing behavior. Instructions, access order, final writeback and fault policies
are unchanged. This fixes callback-visible state; it is not a speedup claim.

Native regressions cover both loads/stores and all four addressing directions,
checking the original base and current flags during failed memory callbacks and
exceptions, and exact final writeback/count. The two fault diagnostics that exposed
the issue now match native: all 672 interpreter-only entry cases (previously 176
callback differences), and all 96 five-instruction region-block-spans cases
(previously 64 callback differences). The latter has MOVS, three scalar reads and
a final four-register LDM/STM with writeback, including page crossings and partial
successful transfers.

Standalone baseline acceptance:
- 138 WASM tests, three native CTest targets, seven frontend checks.
- All ten explicitly rebuilt fault modes: 4,272 exact native comparisons, including
  callback registers/flags/events, final memory and instruction counts.
- Interpreter-checked replay: all 1,600 images, guest records and 4,919,249 stereo PCM
  frames match native. The known native-identical movement heuristic remains false.

Archive /home/claude/.scratch/eka-benchmark/block-writeback-baseline, based on
b0237a687 plus its saved patch. WASM SHA-256:
785bcd210102da32d7416ce45b8af735a540dd0bc3281b9767eee859b758bd6b.
BLOCK_WRITEBACK_CALLBACK_EVIDENCE.json records source, probe and binary hashes,
exact comparisons and logs. The region-memory optimization is absent from this
archive, providing a baseline with the same two correctness fixes.

Reproduce: explicitly build eka_cpu_fault_native and eka_cpu_fault_wasm, run each
with --extended, --deferred, --entry-budget, --entry-budget-deferred, --read-spans,
--wide-snapshots, --region-spans, --region-spans-interpreter,
--entry-budget-interpreter and --region-block-spans. Compare with
compare_cpu_faults.py --require-equal and the counts in the evidence.
No live acceptance or deployment accompanies this fix.
