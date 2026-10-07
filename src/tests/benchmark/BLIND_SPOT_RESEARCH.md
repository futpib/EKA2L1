# Performance opportunities outside the archived experiment sweep

Research on 2026-10-07, alongside pane `%30`'s controlled reassessment. This is
source inspection, reanalysis of existing Chrome captures, and disassembly of
retained guest code. No new game timing, optimization, deployment or input to
that pane was performed. Source/profile hashes and the offline checks are in
[BLIND_SPOT_RESEARCH.json](BLIND_SPOT_RESEARCH.json).

The strongest concrete compiler finding is an unnecessarily long lifetime in
the memory-proof analysis: a late write to a pointer register rejects earlier
accesses through its unchanged value. The broader research opportunity is to
retain guest-level facts through memory operations and region boundaries, while
keeping precise state at observable exits. This is different from reducing raw
WASM opcode counts or reviving a previously rejected implementation unchanged.

## Evidence and its age

Both captures below use direct memory 2 and have diagnostics disabled. Percentages
are self-sample time divided by the selected worker's sampled span, including
waits and profiling margins. They are not utilization, speedup estimates or
disjoint attribution of everything in an inlined runtime function.

| # | Existing capture | Generated ROM | Generated RAM | Generated private/unnamed | Outer loop including inlined work | RAM lookup |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes, 78–96 guest seconds, Oct 6 dispatch/division baseline | 10.15% | 41.86% | 0.43% | 18.69% | 11.96% |
| 2 | Sky Force combat, 42–48 guest seconds, Oct 6 `bf12facd3` | 23.99% | 9.70% | 0.30% | 40.19% | 2.47% |

Generated code totals 52.44% of the Snakes span and 33.99% of the Sky Force
span. The Sky Force scheduler region at `0x8019d818` alone has 9.24%; the
syscall-handler wrapper has another 5.61%. Snakes' division entry at
`0x80191968` has 5.20%, and its fixed-point math entry at `0x7006370c` has 1.84%.
The RAM labels identify translated entries, not proven executable ownership.

These captures precede the compact dispatch cache and/or later state cleanup.
They locate candidate work; they do not describe the final current binary or
establish the remaining cost of an already optimized lookup. The important
counterexample to an emission-only strategy is the large Sky Force runtime
bucket. The important counterexample to a ROM-only strategy is Snakes' RAM share.

## Ranked next investigations

| # | Opportunity | What the current compiler/runtime misses | Confidence and smallest discriminator |
|---:|---|---|---|
| 1 | Lifetime-scoped memory proofs | A pointer need not remain unchanged for the entire region to justify several accesses through one value | A concrete missed hot sequence is confirmed. Count affected hot accesses, then test its complete fast/fallback paths in one kernel |
| 2 | Predicate/flag recipes across successful memory accesses | Flags can remain a comparison recipe until a consumer or observable exit needs concrete architectural flags | The old failed recipe experiment stopped at memory. Inspect executed native instructions in the Sky Force scheduler before implementing a broader version |
| 3 | Complete arithmetic-helper summaries | A software algorithm can be replaced, preserving its architectural effects, rather than optimizing a few individual digit steps | Division is demonstrably hot, but there is no validated replacement or speedup. First derive and check the whole helper contract offline |
| 4 | Specialize kernel dispatch and avoid redundant same-thread state copies | OS-version decisions and common SVC bindings are stable; rescheduling need not imply a different CPU context | Source contains the repeated work; frequency and removable CPU share remain unmeasured. Attribute the syscall bucket and count same-thread switches |
| 5 | Hot call-site connections with precise live-state contracts | Current generic region calls publish/reload state and choose a target through a common runtime site | Architectural opportunity only. Use actual hot edges; prior static ROM grouping and larger regions are negative controls |

### 1. A concrete sixteen-load proof exclusion

The retained 57-instruction math kernel begins at `0x7006370c` and returns at
`0x700637ec`. It performs sixteen immediate word loads at offsets 0 through 60
from the incoming `r1`. The first instruction that overwrites `r1` is the final
load itself:

```text
ldr r6, [r1, #0]
... loads at #4 through #56, with multiply/accumulate and stores between ...
ldr r1, [r1, #60]   # address still uses the old r1
... use the loaded result; no more accesses through the old r1 ...
```

The offline check disassembles the actual retained bytes, checks all sixteen
offsets, and verifies the first `r1` write. It also records the subsequent
arithmetic writes to `r1`; those occur after the final address use.

In [`arm_translator.cpp`](../../emu/cpu/src/aot/arm_translator.cpp), the
entry-proof analysis first unions every register written anywhere in the region,
then refuses a memory operand whose base is in that union. Consequently it
rejects all sixteen of these loads. The same routine already has four stores
through the unchanged `r0`, illustrating why a global read/write exclusion loses
useful cases even in a region that otherwise has proofs.

Candidate: represent pointer values by definition/lifetime, retaining the entry
value through its last address use. Check the 64-byte readable span once and
use host-pointer-plus-constant accesses in that lifetime. Separate proofs can
cover copied pointers and constant adjustments. Start with this straight-line
case, without adding another generic runtime page cache.

The proof establishes address translation, permissions and lifetime, **not that
the data remains constant**. Keep the loads and intervening stores in their
original order: output and input may alias. Do not preload sixteen words,
assume non-aliasing or perform a wide SIMD load merely because the span is valid.
Keep wrap, page, endian, partial-budget and helper behavior; leave to the precise
path if a required proof fails. A helper/remapping boundary ends the proof.

This differs from lowering the old global threshold from four accesses to two
or three, which introduced setup for marginal spans. Here there are sixteen
uses of one value. Actual proof success, native savings and whole-game coverage
still need measurement. The 1.84% region sample is not the fraction removable by
this change, and this one routine alone is not a large whole-game opportunity.

### 2. Scheduler predicates, not a global flags-cache retry

The actual ROM contains this loop inside the sampled scheduler region:

```text
cmp      lr, r4
sub      r0, lr, #12
ldmdane  lr, {r2, ip, lr}
beq      queue_empty
cmp      r2, #0x80000001
andsne   r3, ip, #1
beq      loop
```

The compiler emits architectural flag operations and preserves precise state
around accesses. The proposed investigation is whether this hot path can carry
the necessary predicates directly, reconstructing other flags only on exits
that observe them. Successful inline memory does not by itself require the
same publication as a helper callback. Failed/missing mappings still do.

The [older deferred-flags experiment](FLAG_DATAFLOW_RESULTS.md) materialized
recipes before every memory instruction; its archived scalar ARM loads used
helpers. Its controlled Snakes loss does not test recipes retained across
today's proven memory fast paths, or this Sky Force loop. Conversely, the
synthetic flag wins do not establish that this loop benefits. Inspect the native
hot path first: V8 may already eliminate part of the apparent flag work.

The current compiler also already retains wide multiply results through a narrow
class of restartable loads and ALU operations. That mechanism in
[`arm_translator.cpp`](../../emu/cpu/src/aot/arm_translator.cpp) is an existing
model for precise exit reconstruction, not a new optimization to claim again.

### 3. Whole helper semantics, not whole-register memoization

The [division census](DIVISION_CENSUS_RESULTS.md) finds substantial operand reuse,
but almost no identical complete CPU inputs. Unchanged unrelated registers
should be passed through, not included in a numerical cache key. Scratch
outputs, flags and path-dependent instruction counts still need their true
dependencies established; the mathematical quotient/remainder is insufficient.

The [digit-group experiment](DISPATCH_AND_DIVISION_RESULTS.md) replaced only
selected triplets inside the algorithm. Its common sample reduced executed
WASM operations by 25.3%, but it barely reduced whole-game retired instructions.
This does not establish the effect of replacing the complete helper, including
its branches and region transitions.

A bounded next discriminator derives a summary from the exact helper bytes:
all modified registers/flags, return behavior, signed overflow and divide-by-zero
paths, and the guest instruction count. Verify it against the original for edge
cases and randomized inputs. Retain the original for short budgets and cases
not covered by the proof. Only then compare complete native execution cost;
count reconstruction or extra guards could erase the gain. Do not infer a safe
implementation from the ABI permitting caller-saved clobbers: the emulator can
observe those registers at a precise exit.

Prefer a generic recognized algorithm or a versioned, byte-validated library
implementation, never a game-PC shortcut. No summary or narrowed cache was
implemented in this research.

### 4. Runtime work outside the translator

[`kernel.cpp`](../../emu/kernel/src/kernel.cpp)'s SVC wrapper repeatedly selects
EPOC 9.1/EKA1 return conventions even though the OS version is fixed.
[`libmanager.cpp`](../../emu/kernel/src/libmanager.cpp)'s `call_svc` performs a
hash lookup, takes a callable snapshot and checks logging each time. Investigate
selecting the version-specific wrapper once and a direct common-ordinal table.
Keep rare/trampoline handling and callable lifetime across registration changes;
the callable snapshot was deliberately retained by an earlier correctness-aware
optimization. This is separate from compiling the guest SVC instruction.

[`scheduler.cpp`](../../emu/kernel/src/scheduler.cpp)'s `reschedule` unconditionally
calls `switch_context`, which saves and reloads CPU state even when old and new
thread pointers are equal. A same-thread fast path may avoid those copies while
retaining time-slice, ready-queue, accounting and lifetime effects. Existing
Snakes service evidence found only its main guest thread executing during play,
but that does not count equal-pointer switches: idle transitions are possible.
Measure that condition before implementing it. Its own sampled share is modest.

Do not skip scheduler service, change guest time or remove kernel locking merely
to avoid this work. The target is redundant implementation work under the same
guest scheduling contract.

### 5. Connections chosen by hot edges and state liveness

The runtime's generated-call interface is `(cpu*) -> instruction_count`. A
non-fused guest call/return updates CPU state, returns to the runner and resolves
the next region. This hides guest register relationships across that boundary.
The ordinary handoff remains WASM-to-WASM; JS is involved in installing new
modules, not in every translated guest call.

One future discriminator is a hot RAM call/return pair with a narrow live-state
contract and a guarded target, preserving registers in WASM values until a true
exit. It must remove more work than its target guards and register pressure add.
Static ROM grouping, retaining the entire union of group registers, a larger
region cap, and indiscriminate inlining have already been investigated; they
are not fresh proposals. The [cohort census](ROM_STATE_COHORT_RESULTS.md) retained
only about 7.2% of Sky Force boundaries internally and 0.04% of Snakes boundaries.

V8 already performs speculative indirect-call inlining; its published design
uses call-site target feedback and documents an instance restriction. Our common
dispatcher and separately instantiated modules are a poor fit for expecting it
to recover guest call structure automatically. That is a design inference, not
a measurement of the installed engine's exact inlining decisions. Verify those
decisions before restructuring modules. [V8 design](https://v8.dev/blog/wasm-speculative-optimizations)

## How to avoid another low-payoff experiment campaign

The current controlled sweep answers whether archived implementations were
misjudged. It does not establish their marginal effect under today's memory,
state and runtime defaults. Do not rerun the whole archive to decide the new
lifetime-proof question.

For each new candidate, first establish hot coverage and a concrete native
mechanism. Then use a small exact-state/fault kernel discriminator. Only a
candidate with real native-path savings should get a matched current-build game
comparison. Rejecting every transformation that adds any work to a cold path
would unnecessarily exclude useful guarded specializations; correctness and
expected runtime cost are separate gates. Bound cold-path overhead explicitly.

The major attribution gap is still native instruction location. These CDP
captures provide entry positions for hot generated functions, not a distribution
over their ARM instructions. Static whole-function native inventories include
cold fallback code and are not dynamic spill counts. Whole-worker retired
instructions cannot identify branch misprediction, cache misses or dependency
stalls as the cause of a regression.

Use a separate native sampling capture with V8 JIT metadata to split the hot
runner and generated paths, then add offline emitter provenance from WASM byte
ranges to guest PCs/helper/guard/state-transfer categories where needed. Collect
branch/cache or available top-down counters only to discriminate a specific
hypothesis. Profiling captures remain separate from timings and retain normal
tiering; forced-TurboFan disassembly is an inspection control, not evidence of
the tier actually executing during normal gameplay.
[V8 perf integration](https://v8.dev/docs/linux-perf),
[compilation and profiling behavior](https://v8.dev/docs/wasm-compilation-pipeline)

Audio/offloading, generic bulk acceleration, whole-register division memoization,
ordinary local-copy cleanup, and another whole-ROM grouping retry have weaker
support for these workloads. Prior service tracing found little expensive
unawaited work, and the generic bulk candidates had little measured gameplay
coverage. Those negative results are workload-specific; they are not universal
claims about Symbian games.

The first implementation candidate is the lifetime-scoped memory proof. The
next measurement should resolve Sky Force's large runtime bucket. Neither
requires a new optimization framework or a broad batch
of guessed variants. This report establishes candidates and discriminators,
not new measured speedups.
