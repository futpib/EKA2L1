# Fold immutable ROM syscall stubs into Thumb callers

Adopted on `wasm-port`.
This extends the adopted [literal-PC veneer folding](ROM_LEAF_HANDOFF_RESULTS.md)
to unconditional ARM SVC stubs. A Thumb caller can publish the pending syscall
directly, eliminating the lookup/call/return through the separate ARM region.

## Combined controlled result

| # | Game | Worker CPU seconds, control → combined | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.0238 → 7.0728 | -0.69% | -0.81% | +0.02% | 2/4 |
| 2 | Sky Force | 24.5830 → 22.5626 | +8.95% | +7.99% | -4.06% | 4/4 |

This is a direct comparison of **both handoff optimizations together** against
the original `6eb59dbc2` runtime. It is not the incremental effect of syscall
folding alone, and the earlier literal-veneer percentages must not be added.
The candidate source starts at `2fb8a3b85` plus the archived syscall patch.

The final implementation is committed as `0b6aabed4`. It is active under the
existing default compiled-SVC and Thumb-memory policies, with no new selector.

Four fresh launches per build per game use ABBA then BAAB order. The fixed
gameplay windows are Snakes 78–96 guest seconds and Sky Force combat 42–60.
Guest progress, instruction counts and presentation journals match. All valid
runs, including slower pairs, remain in the result. Source maps and sampling
are disabled in both normal timing builds. Percentages measure throughput.

There are 16 valid observations and 0 retained invalid attempts.
The guest worker uses CPU 7, with sibling 15 reserved and a requested 3.6 GHz
clock. Prospective clock, throttle, placement and counter checks apply; there
are no temperature gates or cooldowns. Host settings, CPU placement, platform
profile and charging settings were restored; all 88 live restoration checks pass.
See [the method](CONTROLLED_BENCHMARKS.md) and [full evidence](ROM_SYSCALL_HANDOFF_RESULTS.json).

## Removed boundary and preserved semantics

Before, the Thumb BLX caller returned to the C++ runner, which found and
called the ARM SVC region. That region published the pending syscall and
returned to the outer C++ loop. Now the caller publishes the same pending
syscall and returns directly to the outer loop. The actual syscall handler,
thread scheduling and associated CPU-state behavior are unchanged.

The compiler accepts only bounded, already-fused Thumb BLX immediate calls
whose immutable ROM target is an unconditional ARM SVC instruction. The
existing compiled-SVC policy must be enabled. Both TLB and direct-memory
backends support the new case. Conditional SVCs, incomplete instruction
windows and all other targets keep their original paths.

The caller preserves both call-half budget boundaries, LR, ARM mode, flags,
short-budget fallback, the ordinary SVC single-step fallback, page-end marker
and cumulative guest instruction count. The preceding call-half stop/IRQ check
has no intervening callback or memory access before the fused SVC. The
successful path uses the existing pending-SVC protocol and stops the compiled
chain. No runtime selector, per-call counter or JS round trip is introduced.

## Actual warmed native code

The normal combined build was sampled with ordinary V8 tiering; all 160
selected native versions were recovered without snapshot errors or lost
samples. The original control capture is reused from the literal-veneer
experiment because it is the identical frozen control artifact. A scratch
harness permits mapless captures for this code-shape audit; this does not
claim new source-line attribution or controlled profile timing.

| # | Thumb caller | Original ARM target | SVC immediate | Native caller bytes, before → after |
|---:|---|---|---|---:|
| 1 | `0x801a6602` | `0x8019dc50` | `0x000005` | 1,408 → 1,536 |
| 2 | `0x801b926e` | `0x8019db70` | `0x800005` | 1,408 → 1,536 |
| 3 | `0x801a637c` | `0x8019db48` | `0x800000` | 1,408 → 1,536 |

All three sampled wrappers now store the pending-SVC word and logical count
four, then return zero via the existing trap protocol. Previously they
returned three and left the ARM stub for a subsequent lookup and call.
Static function size is not an executed-instruction count or speedup estimate.
Code hashes, version identities and full native disassemblies are preserved.

## Correctness and live deployment

The combined build passes 194,560 full-state comparisons against the original
caller plus separately compiled ARM SVC region. The matrix covers both memory
backends, entry-budget layouts, compiled-SVC settings, call alignments, prefix
instructions and flags, syscall ordinals, a conditional negative case, short
budgets, page-end targets, stop/IRQ masks and 64-bit remaining counts.
It also passes 9,216 existing SVC boundary cases, 34,560 literal-veneer cases
and 288 call-half boundary cases. The earlier literal-veneer stage passed the
179-test full AOT suite; the complete suite was not repeated for this extension.
Both real 60-frame game replays match reference images, guest progress, audio
PCM and audio events exactly.

The measured combined artifact is served at `https://claude-laptop.lan:8188/`.
The served WASM hash matches the timed build. Both real game-picker paths
pass gameplay/input/default-policy checks with hardware NVIDIA rendering;
the saved gameplay screenshots were inspected. The existing browser audio
failure remains (audio clock does not advance), so full live-audio E2E is
not claimed. The deterministic PCM replays pass.

Raw plans, builds, commands, profiles, observations and the reproducible source
patch are in `/home/claude/.scratch/eka-rom-svc-handoffs`. Source and artifact
hashes are recorded independently of subsequent documentation changes.

## Realtime speed and next measured target

At the controlled 3.6 GHz clock, Snakes runs at 2.09× realtime and
Sky Force combat at 0.72×. These use actual wall time, not worker
CPU time: 18 guest seconds divided by the measured elapsed time.

The post-change native capture still has 31.91% of worker samples in
`execute_chain`, 10.61% in the outer interpreter loop, 10.50% in the generated
EUser scheduler region and 4.83% in `call_svc`. Within the runner, the inspected
call machinery, sparse ROM lookup and return/bookkeeping ranges contain
11.43%, 8.54% and 5.12% of the worker samples respectively. These are remaining
sampled costs from one capture, not recoverable percentages or controlled
before/after cost reductions. No source-line interpolation is used.

The next concrete candidate is the return side of the same wrappers. The
actual ROM contains `BX LR` immediately after each of the three SVCs, followed
by `POP {r4, pc}` in its Thumb wrapper. All six separate return regions have
native samples in the combined capture. After a syscall, execution still goes
through the ARM return region and then the Thumb return region before reaching
the actual caller, paying the runner boundary again between them.

The subsequent [return-continuation experiment](SVC_RETURN_CONTINUATION_RESULTS.md)
implements and measures this follow-up. Its C++ WASM continuation consumes
the returned syscall state, then executes the proved BX/POP pair directly.
This can eliminate both generated-region calls while keeping the syscall
handler as a scheduling boundary. See that report for scope, fallbacks,
incremental gains/losses and adoption status.
