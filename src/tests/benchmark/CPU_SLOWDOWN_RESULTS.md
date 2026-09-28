# Zooming into the native / WASM CPU gap

Source baseline: `9f7938e34`; research tools: `76cedd622`, `wasm-port`. Research only: no emulator performance
change is deployed. The tested exit-flush prototype is preserved as
`precise_exit_flush_experiment.patch` and removed from production.

## Finding

The strongest measured kernel cost is **our emitted memory machinery**, followed
by exact per-instruction budget handling. Moving the unchanged hot function into
the caller's WASM instance gives a much smaller gain. A real native-code dump
shows substantial state loads/spills and expanded guard/exit code, but shrinking
cold exit code alone did not help. This narrows the next compiler target; it does
not establish a new whole-game speedup or a universal WASM penalty.

The prior whole-game profiles remain the outer context: generated execution
12–14 sampled seconds in WASM versus 3.4–4.4 native, plus 6–8 browser seconds in
lookup/validation and outer dispatch over 18 guest seconds. Those were separately
sampled runs, not additive causal estimates. This investigation isolates two
actual hot code sequences to explain some of that gap, rather than repeating
another broad whole-game profile.

## Same kernel, state and work

The offline tool extracts the existing 57-instruction fixed-point math routine
at `0x7006370c` and the seven-instruction **prefix**, not the full function, at
`0x70013edc`. These PCs select research fixtures only. No production game/PC
whitelist was added.

Native uses the repository's Dynarmic with the same v6T2 and TLB settings as Qt
for these integer instructions. For each of 16 seeded RAM fixtures, native
`Step()` supplies exact registers, CPSR, instruction count and all data-memory
bytes at every budget from zero through the full kernel. Current WASM, optimized
WASM and the safe exit-flush prototype match these states. Diagnostic flat-memory
variants match only within this ordinary-memory fixture; full-budget-only
variants are checked only at full budgets. The final browser run makes **5,344
exact comparisons** (4,672 math, 672 prefix).

Native `Run()` is separately required to match the full-budget Step result and
instruction count for every seed before timing. A zero-instruction terminal at
the fixture boundary prevents speculative native linking from adding an extra
instruction outside the measured kernel. An initial attempt without that terminal
executed 58 rather than 57 instructions and was rejected. Exact partial native
`Run()` exits are not assumed; `Step()` is the oracle for partial states.

This is equivalent work on the tested full-budget, aligned, permitted,
little-endian ordinary-memory path. **Fault, permission-failure, unaligned,
concurrent code-change and alias-write paths are not exercised by the kernel
fixture.** The standard emitter retains them; the diagnostic ablations do not.
No restricted IR-emitter timing with weaker semantics is presented as a fair
native replacement. The existing emulator regression suite covers those paths
separately, but it does not make the ablations safe.

## Controlled browser decomposition

Chromium 153.0.8010.52 on the same i7-10875H. No GPU rendering, debugger or
sampling in kernel timing. Each variant warms for 500,000 calls, then runs eight
alternating-order trials of five million calls. A WASM loop restores the same
16 registers per call; there is no JavaScript call per iteration. Timing includes
that common driver cost. Separate inspection runs are excluded.

Medians in milliseconds for five million calls, final batch:

| Variant | Math (57 instructions) | Prefix (7 instructions) |
|---|---:|---:|
| Current emitter, cross-instance imported function | **585.135** | **118.148** |
| Binaryen `-O3`, same semantics (ablation control) | 589.417 | 123.470 |
| Remove per-instruction budget checks, full-budget-only diagnostic | 465.580 | 114.830 |
| Flat identity memory, remove translation/permission/alignment/code-write guards, diagnostic | 270.360 | 64.358 |
| Both diagnostic removals | 200.778 | 65.415 |
| Current emitter, direct call within the same WASM module | 582.120 | 111.543 |
| Safe precise exit-flush prototype | 591.943 | 123.317 |
| Empty imported function + register-reset loop (driver control) | 39.068 | 38.790 |

The check-removal variants all receive `-O3`, so compare them against the `-O3`
row, not silently against a differently optimized baseline:

- Math memory diagnostic removes **54.1%** of elapsed kernel time; budget removal
  alone removes **21.0%**; both remove **65.9%**. These savings **overlap** and must
  not be added. They include changed compiler lowering/register pressure, not
  merely the direct latency of deleted branches.
- Prefix memory diagnostic removes **47.9%**; budget removal only **7.0%**. The
  combined result is slightly worse than memory-only, illustrating interactions.
- Same-module call gains **0.5% math / 5.6% prefix** in the final batch. Earlier
  independent batches gave roughly **1.5–1.6% math / 4.8–6.4% prefix**. This tests
  a direct imported call versus direct intra-module call. It does **not** measure
  the real polymorphic table dispatcher, code validation, or linking whole loops.
- The empty driver costs ~39ms, so it matters more for the short prefix. Do not
  subtract it as a perfectly independent component: optimization and scheduling
  differ when the callee does useful work.

All diagnostic outputs live outside the served build. Removing these guards is
not an optimization proposal. The fixture has identity-mapped data addresses;
using its flat variant on real guest memory would be incorrect.

## Native kernel controls and their limits

The same five-million-call test through `Dynarmic::Jit::Run()`:

| Configuration | Math median ms | Prefix median ms |
|---|---:|---:|
| Native TLB, first final batch | 309.883 | 329.361 |
| Native fastmem diagnostic | 326.544 | 325.016 |
| Native TLB, repeat with minimal-call control | 322.816 | 330.548 |
| Native one-NOP fixture through the same Run API | 298.043 | — |

Fastmem did not improve this native fixture. The native generated memory path
already uses direct host loads/stores after a compact TLB tag check. This is not
a clean hardware-memory-latency subtraction; different configurations also alter
register allocation and code layout.

The math comparison is about 1.8–1.9x slower in WASM through these harnesses, while
the seven-instruction prefix is faster in WASM. That does **not** contradict the
whole-game Qt advantage. Native Run enters/exits the JIT from C++ every kernel
call here; normal Qt links blocks and amortizes that machinery over long runs.
The one-NOP control demonstrates substantial call-path cost, but it uses a
different terminal from the math return and **must not be subtracted** as an
exact native dispatch constant. These microbenchmarks must not be extrapolated
into a whole-game Qt/WASM multiplier.

## Actual generated machine code

A separate Chromium `--perf-prof` run records complete TurboFan code records.
Native code is copied from Dynarmic's named perf-map ranges while the JIT is
alive. Native single-step blocks and full Run blocks are kept distinct; the
sizes below are full Run kernel blocks. JITdump's incomplete trailing record is
explicitly ignored; only preceding complete records are used.

| Kernel / emitter | x86-64 bytes |
|---|---:|
| Math: native Dynarmic block | **3,153** |
| Math: current WASM via TurboFan | **37,696** |
| Math: precise exit-flush prototype via TurboFan | **33,280** |
| Prefix: native Dynarmic block | 649 |
| Prefix: current WASM via TurboFan | 6,144 |

The current math function reserves 224 stack bytes in its prologue, eagerly
loads guest registers and runtime guard fields, and spills several values before
the first guest memory access. Its emitted memory path checks cached page,
permission/tag, alignment/endian state, and code-write overlap before or around
ordinary accesses. Dynarmic's observed block also performs TLB checks; native is
not magically free of address translation. Its generated block has a much
smaller state/exit envelope and uses native block scheduling rather than our
per-instruction exact exits.

The current WASM dump has 318 static conditional branches and 2,492 static stack
operands versus 2,124 stack operands after the prototype. Many belong to cold
fallback/exit paths. **These are not executed instruction counts, spill-time
percentages, or predicted speedups.** The timing rejection below is direct
counterevidence to treating code-size reduction as sufficient.

## Tested general data-flow prototype

In acyclic ARM regions, an exit need only flush registers written before that
lexical point; the old deferred flush uses the final whole-region write set.
The prototype records the write set at each barrier. Backward edges retain the
old conservative behavior; callback reloads still use the final full local set,
so callbacks can modify registers used later. No instruction budget or memory
check is removed.

The prototype passes all shared kernel budget/state/memory comparisons, reduces
math machine-code size by **11.7%**, but gives less than 1% improvement in an
earlier batch and regressions/overlap in the final batch. **It is removed from
production.** It did not earn expansion or a whole-game speedup claim. The saved
patch and emitted binary hashes make the negative result reproducible. It has
not been given a full-game replay or broad fault/alias regression endorsement.

## Grounded next target

1. Improve the **hot emitted memory path and its data flow**, retaining precise
   exits. The kernel experiments isolate a large cost there, unlike the small
   direct module-boundary effect. Inspect whether an optimizing guest IR can
   reuse address/permission facts, shorten live ranges and defer guest-state
   materialization without changing callback/fault/code-write behavior. Previous
   broad span/guard tweaks and this cold-exit reduction did not establish gains;
   a new prototype must show a measured kernel gain before game integration.
2. Separately reduce **real lookup/validation/dispatch** around generated regions.
   The prior whole-game profile attributes 6–8s there. Our same-module fixture
   bypasses that dispatcher entirely, so its small gain does not rule out
   connected hot paths that avoid actual lookups and state round trips.
3. Keep exact budgets. Their math-kernel ceiling is material, but dropping them
   would change semantics. Budget-specialized fast paths need a precise fallback
   and whole-game evidence; earlier blanket grouped-budget changes failed to
   establish a gain.

No new whole-game improvement, Qt parity, or 1.25x heavy-window headroom is claimed.
The served build and safe-versioning opt-in setting are unchanged.

## Reproduction / validation

Build optional native targets `eka_compiler_probe` and `eka_cpu_kernel_native`.
Use `eka_compiler_probe GAME_EXE NEW_KERNEL_DIR` to extract the fixtures and current
emitter. Use `eka_cpu_kernel_native KERNEL_DIR NEW_NATIVE_DIR` for native state
outputs and timings; optional `--fastmem` is an ordinary-memory diagnostic.
`EKA_KERNEL_DUMP=1` additionally saves named native code ranges.

Run `pack_cpu_kernel_fixtures.py NATIVE_DIR KERNEL_DIR`, then
`cpu_kernel_variants.py KERNEL_DIR --bin SDK/upstream/bin --driver
src/tests/benchmark/cpu_kernel_driver.wat`. Run
`node src/tests/wasm/cpu-kernel-breakdown.ts KERNEL_DIR NEW_OUTPUT`.
If `*.precise.wasm` files are present for both kernels, the packer enables that
prototype comparison. Create them by applying the saved experiment patch only
to an isolated research build of the extractor, then restore the source.
`EKA_KERNEL_INSPECT=1` skips correctness and shortens inspection runs; their
reported timings are diagnostic and excluded. Use `EKA2L1_V8_FLAGS=--perf-prof`
for actual V8 code, and `inspect_jitdump.py` for extraction.

All three native test targets and all 132 tests in the unchanged default WASM
suite pass after restoring production source. Kernel comparisons test the new research harness directly.
No new full-game replay was necessary for deployment because production emulator
sources and the served WASM binary are unchanged. Existing 1,600-image evidence remains scoped to
its recorded build. Detailed reports, hashes and raw paths are in
`CPU_SLOWDOWN_EVIDENCE.json`; proprietary guest code and raw generated binaries
remain in local scratch, not in Git.
