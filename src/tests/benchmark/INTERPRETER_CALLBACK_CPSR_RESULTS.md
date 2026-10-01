# Interpreter flags before slow memory callbacks

The whole-region memory-proof prototype exposed an existing interpreter defect.
After its entry proof returned without guest effects, the runner could execute
an entire interpreter block. MOVS changed unpacked flags; a later memory exception
callback still saw the old packed CPSR. The prior compiled-to-interpreter boundary
fix synchronized flags before that block, not after arithmetic inside it.

Reproduced with compiled execution disabled: MOVS followed by four word loads,
three initially permitted and the last potentially faulting. Of 48 native/WASM
cases, 32 had callback CPSR differences although all final states and memories
matched. The new fix publishes NZCVT at the eight slow data-memory entry points,
before memory callbacks and possible exception callbacks. Mapped direct accesses
are unchanged. Cpsr is mutable because packing this flag cache is necessary from
const read accessors; it updates this ARMul_State, not a different owner's state.
The fix does not alter writeback order or exception policy.

Acceptance on the standalone fixed baseline:
- 138 WASM tests; all three native CTest targets and seven frontend checks.
- New native regression checks memory and exception observers after MOVS within
  one run, across byte/halfword/word loads and stores.
- All six existing fault modes, 3,408 cases, match native exactly.
- New region-spans and region-spans-interpreter modes: 48 cases each, exact native
  state, memory, callbacks and instruction counts. Total exact cases: 3,504.
- Checked replay: all 1,600 images, guest records and 4,919,249 stereo PCM frames
  match native. The known native-identical movement heuristic remains false.

A broader interpreter-only two-instruction diagnostic retains 176 known block
writeback callback differences out of 672 cases. All final states/memory match;
the differing callbacks concern the base register for LDM/STM with writeback,
not CPSR. All 288 scalar cases match, as do the other 208 block cases. This fix
is not a claim of complete interpreter/native callback equivalence. Accordingly,
the memory-proof prototype is being changed to use the original compiled path
on proof failure, preserving that path's writeback ordering. No optimization is
promoted on the strength of this correctness fix.

Standalone archive: /home/claude/.scratch/eka-benchmark/interpreter-cpsr-baseline.
WASM SHA-256: 6eef90895a64321c31a2fc32fb3a82baf69bf76225a5fe0e05e2da0e65452d16.
INTERPRETER_CALLBACK_CPSR_EVIDENCE.json records its base, exact patch/source and
artifact hashes, comparison summaries and logs. The reproduction executable and
logs remain under interpreter-cpsr-repro-probe and interpreter-cpsr-repro.*.
Fault targets were explicitly built; they are excluded from the default build.
Reproduce with compare_cpu_faults.py using the archived probes and the modes above.
No live acceptance, deployment or speedup claim accompanies this fix.
