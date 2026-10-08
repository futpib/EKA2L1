# Incoming-pointer lifetime proofs

Adopted on `wasm-port`: **Snakes CPU throughput improves 1.87%**, with all four
pairs faster. Sky Force averages **+0.14%**, with two faster and two slower pairs;
that does not establish a Sky Force improvement. No experiment toggle is added.

## Runtime measurement

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Paired CPU range | Faster pairs |
|---:|---|---:|---:|---:|---:|---|---:|
| 1 | Snakes | 7.5314 → 7.3932 | +1.87% | +1.35% | -0.94% | +1.48% to +2.12% | 4/4 |
| 2 | Sky Force | 24.7212 → 24.6872 | +0.14% | +0.07% | -0.002% | -1.49% to +2.26% | 2/4 |

All 16 observations are valid; none were discarded or retried. The comparison
uses four fresh launches per build per game, in ABBA then BAAB order, with the
same current defaults and identical frozen browser harness. Snakes runs from
78–96 guest seconds; Sky Force combat from 42–60. Each game's guest instruction
counts, virtual endpoints and presentation journal match between builds.
Sampling, tracing, verification and diagnostic instrumentation are disabled.

The worker is pinned to CPU 7; its SMT sibling 15 is reserved, and support work
uses other CPUs. The requested fixed clock is 3.6 GHz, with measured worker
frequency approximately 3.592 GHz for every valid run. Actual clock, throttle,
affinity and counter checks pass. There are no temperature gates or cooldowns.
The temporary frequency, placement, platform-profile and charging settings were
restored; all 88 live restoration checks pass. See
[benchmark controls](CONTROLLED_BENCHMARKS.md) and the
[complete observations, plan, hashes and validation evidence](POINTER_LIFETIME_RESULTS.json).

The repeatable Snakes improvement is accompanied by fewer retired native
instructions. Sky Force's native instruction count is essentially unchanged,
and its paired timings vary in both directions. These results support adopting
this change for Snakes; they are not a demonstrated two-game speedup, a result
for every scene, or an additive gain over other historical experiments.

This follows the sixteen-load finding in [BLIND_SPOT_RESEARCH.md](BLIND_SPOT_RESEARCH.md).
The candidate compares against `b96296d58` on `wasm-port`, with the same current
browser defaults. It changes generated guest execution, without adding a runtime
experiment selector or instrumentation.

## Implementation

The compiler records which registers have been defined before each instruction.
In a contiguous straight-line region, the entry value remains valid until its
first definition. The defining load itself still evaluates its address using
the old value. Conditional definitions conservatively end that value's lifetime.
The captured 57-instruction fixed-point routine therefore gets one checked
64-byte read span for its sixteen incoming-r1 loads.

The extension requires the direct-memory backend, the full-entry-budget body,
and at least four unconditional immediate word loads through the incoming
value. Each new pointer group must individually reach that threshold. Existing
proofs for registers unchanged throughout the region retain their old rules.
Internal branches, loops, inlined callees, pointer-copy/offset propagation, new
write proofs and Thumb lifetime analysis are outside this extension. Short
budgets enter the unchanged precise body. The TLB backend is unchanged.

Loads and stores execute in their original order, including when outputs alias
inputs or two guest mappings share physical backing. The proof establishes an
address mapping, not constant data. There is no bulk preload, non-aliasing
assumption or special case for a game address. Existing wrap, alignment, page,
endian, permission and physical-code-alias guards remain. A failed span check
uses the original precise fallback before guest effects. A deferred memory
access exits before its helper; a non-restartable helper exits after the whole
instruction, before another access can reuse a cached host pointer.

## Mechanism and limitations

The offline instrumenter counts executed WASM operations throughout exported
and private functions. It excludes structural block/loop/end/else markers,
its own counters and imported helper bodies. Helper traces must match. These
counts are neither V8 native instructions nor timings.

| # | Captured routine path, full budget | Before | Candidate | Change |
|---:|---|---:|---:|---:|
| 1 | Direct page table, aligned span | 1,891 | 1,206 | -36.2% |
| 2 | Direct arena, aligned span | 1,416 | 1,078 | -23.9% |
| 3 | Cross-page span, fallback | 1,891 | 2,420 | +28.0% |
| 4 | Unaligned address, four instructions before exit | 344 | 459 | +33.4% |
| 5 | Missing view, one instruction before exit | 682 | 710 | +4.1% |

The additional guard costs work when it fails or an earlier instruction exits.
This is not an all-path instruction-count reduction or a guaranteed speedup.
Across 25,920 baseline/candidate invocations, 698 shrink, 24,048 are unchanged,
and 1,174 grow. Every invocation matches guest count, CPU state, memory and
helper trace. These deliberately enumerated cases are not game hit rates.
The TLB, entry-budget-reference and backedge probes are byte-identical.

## Correctness and reproduction

The dedicated fixture captures the actual routine bytes, relocates them to an
arbitrary PC, and compares them with the independent ARM interpreter. Synthetic
fixtures cover other base registers, overlapping stores, early and conditional
pointer kills, short lifetimes, backedges and forward joins. There are 5,832
interpreter comparisons plus two helper-remapping boundary checks. Existing
entry-budget checks pass 17,280 cases; shared-span tests pass 25,600 read and
16,896 write cases plus Thumb and callback tests; direct/TLB tests pass 816
comparisons plus real MMU callback/ASID checks.

Both games pass 60-frame comparisons against retained references: exact images,
frame records, guest instruction progress and PCM audio. This does not claim
physical browser audio output was verified.

```sh
node build-wasm/src/tests/aot/test_aot_wasm.js --pointer-lifetimes-only
node build-wasm/src/tests/aot/test_aot_wasm.js --emit-lifetime-probes > candidate.log
node src/tests/wasm/lifetime-counts.mjs baseline.log candidate.log counts.json --breakdown
```

The baseline probe log was emitted with the same test exporter and the unmodified
baseline translator. Game timings use uninstrumented frozen runtime artifacts.
The evidence directory is `/home/claude/.scratch/eka-pointer-lifetime`.

## LAN deployment

The measured artifact is served at `https://claude-laptop.lan:8188/`. Its served
WASM SHA-256 matches the candidate manifest. Both game-picker launches retain
the expected defaults, advance frames and consume keyboard/touch inputs on the
hardware NVIDIA renderer. Gameplay screenshots were inspected for both games.

Full live-audio E2E does not pass: both games still encounter the previously
reproduced host audio-device failure (AudioContext time remains zero). The test
reports only the two non-silent-browser-audio failures; exact PCM replay checks
pass. This optimization does not fix that host issue.
