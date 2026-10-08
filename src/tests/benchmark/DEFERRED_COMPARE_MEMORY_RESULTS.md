# Deferred comparison flags across successful reads

Decision: **not adopted**. V8 executes less flag work, but the game comparison
does not establish a runtime win. The prototype is archived and production
behavior remains at `95391bb99`.

| # | Game | Worker CPU seconds, before → after | CPU throughput | Wall throughput | Native instructions | Paired CPU range | Faster pairs |
|---:|---|---:|---:|---:|---:|---|---:|
| 1 | Snakes | 7.4015 → 7.3877 | +0.19% | -0.03% | +0.01% | -0.22% to +0.53% | 3/4 |
| 2 | Sky Force | 24.5637 → 24.8410 | -1.12% | -1.10% | -2.00% | -4.63% to +1.93% | 2/4 |

Snakes is essentially flat. Sky Force retires 2.00% fewer native instructions,
but its CPU throughput averages 1.12% lower, with two faster and two slower pairs.
The measured Sky Force cycles per retired instruction rise 3.20%.
This describes the observed loss of execution efficiency; it does not isolate
its hardware or generated-code cause. Every valid slow run remains included.
The local native-code saving is real, but is insufficient evidence for adoption.

All 16 observations are valid; 0 invalid attempts are retained.
Four fresh launches per build per game run in ABBA then BAAB order. Snakes is
measured from 78–96 guest seconds and Sky Force combat from 42–60. Guest instruction
counts, virtual endpoints and presentation journals match. Timing builds contain
no profiler, trace, verifier or generated cost counters.

The worker runs on CPU 7, with sibling 15 reserved and support work on other
CPUs. Requested frequency is 3.6 GHz; measured clock, throttle, affinity and counter
checks pass at a measured clock around 3.592 GHz. No temperature gate or cooldown is used. All 88 live restoration
checks pass after restoring temporary clock, placement, platform and charging
settings. See [method and controls](CONTROLLED_BENCHMARKS.md) and the
[complete observations and evidence](DEFERRED_COMPARE_MEMORY_RESULTS.json).

## What survived V8 optimization

A fresh Sky Force capture on baseline `95391bb99` attributes about 9.75% of the
selected worker's samples to `f_2149177368` (`0x8019d818`). This is sampled
function-entry attribution, not per-native-instruction cost or a speedup estimate.
The module was captured after sampling ended. Its scheduler body exactly matches
the independently exported compiler fixture (normalized body hash in the JSON).

Chromium 153.0.8010.52 / V8 15.3.76.13 TurboFan still computes and spills N/C/V
between the first comparison and the conditional LDM. On successful fallthrough,
the next comparison replaces those flags. This work survives V8 optimization;
it is not merely extra WASM that V8 already removes.

The successful loop keeps Z concrete for EQ/NE, saves the comparison operands,
and reconstructs N/C/V only on observable exits. Auditing the actual native
control flow gives these static inventories for four specified successful paths:

| # | Memory / node test | Native instructions, before → after | Stack accesses, before → after |
|---:|---|---:|---:|
| 1 | Arena / magic status | 96 → 80 | 23 → 20 |
| 2 | Arena / low bit clear | 101 → 85 | 22 → 19 |
| 3 | Page table / magic status | 109 → 93 | 23 → 20 |
| 4 | Page table / low bit clear | 114 → 98 | 22 → 19 |

Each path has sufficient budget, a nonempty queue, valid aligned memory, and no
interrupt. These are audited branch paths, not measured game frequencies or
native sampling. The companion JSON retains the assembly, module hashes,
path offsets and audit script. The gated scheduler is byte-identical to the
native-inspected prototype.

Cold-exit reconstruction grows the whole-function WASM inventory from 1,114 to
1,229 operations. Whole-function native size stays 5,336 bytes, and its static
instruction inventory changes only from 1,192 to 1,190. Counting a whole function
would obscure the removed work on the successful loop path.

## Implementation and scope

The archived prototype is a generic ARM compiler rule, with no game address or runtime toggle.
It applies to cached direct-memory regions whose reads can return before a helper.
An unconditional CMP is selected only when a lexical scan finds a supported
read followed by an unconditional CMP/CMN that overwrites the flags. The scan
allows EQ/NE read predicates and external conditional branches, and conservative
non-flag-writing arithmetic. Carry consumers and conditional flag definitions
stop the scan.

Supported reads are immediate preindexed word LDR without writeback and ordinary
multiword LDM without writeback, user-bank transfer or PC operands. Recipe operands
have their own locals, so a load can overwrite an operand register without
changing the saved comparison. Reads remain in their original order.

Z remains concrete; N/C/V are reconstructed exactly at returns, failed memory
accesses, helpers, flag consumers, joins and budget boundaries. A taken edge's
snapshot does not consume the fallthrough recipe. Successful reads neither call
a helper nor expose incomplete architectural state. The precise short-budget
body, TLB backend, Thumb compiler and unselected fixtures retain their existing
behavior. There is no relaxation of faults, permissions, flags or accounting.

The initial broader draft deferred comparisons even when their flags were later
needed at a normal return. Although differential checks passed, it grew 64,128 of
115,200 enumerated paths. That draft was not game-timed. Requiring a later flag
overwrite reduces growth to cold/early-exit cases in the timed candidate.

## Correctness and cost checks

The independent ARM interpreter matches all 9,600 new state/flags/memory/count
comparisons, covering the real scheduler and scalar/multiple-read fixtures,
short budgets, guarded/unsafe code policies, denied mappings, unaligned and
cross-page addresses, endianness, and signed/unsigned boundary operands.

The offline shared WASM cost tools compare baseline and candidate across 115,200
invocations: exact CPU state, memory, guest counts and callback traces all match.
Of these, 4,448 execute fewer WASM operations, 2,624 more, and 108,128 are unchanged.
Those deliberately enumerated fixtures are not gameplay hit rates. Every fixture
outside the selected scheduler/scalar/LDM patterns is byte-identical.

| # | Scheduler fixture, budget 32 | Executed WASM operations, before → after |
|---:|---|---:|
| 1 | Three-node walk, page table | 1,057 → 1,006 |
| 2 | Three-node walk, arena | 879 → 828 |
| 3 | Empty queue, early exit | 238 → 243 |
| 4 | Missing node mapping, exit before read | 276 → 281 |
| 5 | Missing memory view | 125 → 125 |

This is not an all-path instruction reduction or a guaranteed speedup. The
cost instrumenter is used only offline; timed builds contain none of its counters.

Existing suites pass: entry-budget variants (17,280 comparisons), shared ARM
spans (25,600 read and 16,896 write), Thumb spans/direct memory (2,688 and 2,310),
ARM memory (21,146 plus callback-state checks), precise instruction counts
(34,560), and loop budgets (24,192). Both games pass 60-frame exact comparisons
against retained references, including images, frame records, guest progress
and PCM audio.

The [archived patch](deferred_compare_memory.patch) contains the compiler change,
fixtures and offline comparator. Apply its test/exporter portion to the baseline
first to emit `baseline.log`, then its compiler portion and rebuild for the commands
below. None of this experiment remains in the active compiler.

```sh
node build-wasm/src/tests/aot/test_aot_wasm.js --flag-memory-only
node build-wasm/src/tests/aot/test_aot_wasm.js --emit-flag-memory-probes > candidate.log
node src/tests/wasm/flag-memory-counts.mjs baseline.log candidate.log counts.json
```

The baseline probe log uses the same exporter and unmodified baseline translator.
Raw evidence is retained in `/home/claude/.scratch/eka-flags-memory`.


## Baseline restoration

The compiler and existing test source exactly match the pre-experiment revision;
new experimental test hooks are removed. After rebuilding, all six runtime files
match the frozen control hashes. A fresh LAN fetch still matches the control WASM.
No service configuration or LAN deployment was changed for this experiment.
The full candidate remains reproducible from its archived patch and frozen files.
