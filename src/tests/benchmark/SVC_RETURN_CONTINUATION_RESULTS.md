# Complete proved syscall returns without region handoffs

Adopted on `wasm-port`, enabled under the existing normal-play defaults with no new selector.

This extends [ROM syscall-stub folding](ROM_SYSCALL_HANDOFF_RESULTS.md).
After the syscall handler returns, the normal outer loop can directly execute
a compiler-proved immutable ARM `BX LR` followed by Thumb `POP {pc}` or
`POP {one low register, pc}`. A successful continuation skips both generated
return regions and their lookups/calls. It does not execute through the syscall
handler or change thread scheduling. ROM and game files remain stock.

## Controlled runtime comparison

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.0438 → 7.1391 | -1.34% | -1.15% | -0.09% | 0/4 |
| 2 | Sky Force | 22.3909 → 21.8486 | +2.48% | +2.26% | -1.61% | 4/4 |

Adoption prioritizes the measured Sky Force gain over the smaller Snakes
regression. This is a tradeoff, not a demonstrated two-game improvement.

The control is the adopted `af7d76628` default (runtime implementation
`0b6aabed4`), already containing both earlier handoff optimizations. These
are incremental measurements; do not add them to the earlier combined gain.

Four fresh launches per build per game use ABBA then BAAB order. Gameplay
windows are Snakes 78–96 guest seconds and Sky Force combat 42–60.
Guest progress, instruction counts and presentation journals match. Every
valid observation is retained, including slower pairs. Normal timing builds
have source maps, sampling and extra diagnostics disabled.

The four Sky Force pairs range from +0.86% to +5.01%; the Snakes pairs
range from -1.96% to -0.84%. Four pairs do not establish the same gains
on other games, gameplay windows, hosts or V8 versions.

There are 16 valid observations and 0 retained invalid attempts.
The guest worker uses CPU 7, sibling 15 is reserved, and the clock request
is 3.6 GHz. Prospective clock, throttle, affinity and counter checks apply.
There are no temperature gates or cooldown waits. Host frequency, platform
profile, affinity and charging settings were restored;
all 88 live restoration checks pass.
See [method](CONTROLLED_BENCHMARKS.md) and [full evidence](SVC_RETURN_CONTINUATION_RESULTS.json).

## Scope and preserved behavior

The compiler recognizes instruction bytes, without game-specific addresses.
Both the ARM return and following Thumb POP must be in immutable ROM. The
existing bounded Thumb BLX/SVC folding and direct-memory policies must apply.
Page-end SVCs, conditional BX instructions, multiple low-register POPs,
RAM instruction windows and incomplete windows keep the original behavior.

The hint and optional low-register number fit unused bits of the existing
pending-SVC word. It changes an emitted constant without adding generated
WASM instructions, CPU fields, counters or another store.

After the handler, the runtime checks returned PC/mode/LR and remaining
budget. It retains the exact BX boundary, including short budgets and
stop/IRQ behavior. The POP runs only after a complete readable stack-span
proof using the freshly published direct-memory view. The existing affine
arena and readable page aliases are supported. Failed span proofs, unusual
endianness and missing views leave POP to its original implementation,
preserving partial accesses and fault callbacks. Registers, flags, PC/mode,
SP and guest instruction counts match the original two-region execution.

The shortcut applies only to normal chained execution. Instrumented dispatch
and unchained single-region execution retain their original paths. The real
core test found that unchained execution publishes memory state differently;
the explicit chaining guard preserves its callback behavior.

This removes WASM-to-WASM region calls. It introduces no JS round trip.

## Actual warmed V8 code

Chrome 153.0.8010.52 / V8 15.3.76.13 was captured with ordinary gameplay
tiering using the exact timing artifact. The capture recovered 159 of 160
selected native versions, with no snapshot errors, sampling errors or lost
samples. One cold RAM Liftoff version, carrying three samples, was unavailable.
The control capture is reused from the identical prior adopted artifact.
These captures establish native code shape and sampled locations, not a
controlled timing comparison or new source-line attribution.

The hot tick wrapper remains 1,536 native bytes and publishes the new hint.
The outer loop grows from 164,608 to 166,016 native bytes. Its new continuation
is inlined and contains no call instruction: it checks the stack span, restores
registers/PC/SP, accounts for one or two guest instructions, and dispatches.

| # | Return region (Thumb tags retained) | Control samples | Candidate samples |
|---:|---|---:|---:|
| 1 | `0x8019dc54` | 78 | 12 |
| 2 | `0x8019db74` | 75 | 1 |
| 3 | `0x8019db4c` | 45 | 0 |
| 4 | `0x801a6609` | 90 | 23 |
| 5 | `0x801b9275` | 109 | 0 |
| 6 | `0x801a6383` | 70 | 0 |

Together these six regions fall from 467/27,522 worker samples (1.70%)
to 36/23,679 (0.15%). Residual fallbacks remain. These are sampled locations,
not call counts or recoverable speedup percentages. Native version identities,
hashes and relevant actual instruction excerpts are included in the JSON.

## Correctness and deployment

The full WASM AOT suite passes **183 tests, zero failures**, with its existing
diagnostics-disabled skips and crash-repro expected failure unchanged.
New coverage includes 600 compiler-eligibility cases, 14,976 full-state
comparisons with the original separately generated returns, and 480 real-core
comparisons using production generated-function installation and SVC handling.
It covers changed syscall state, budgets, IRQ/stop, flags, remapped/aliased
memory, unaligned accesses, page/arena edges, partial faults and callback
traces. Both real 60-frame game replays match reference images, guest
progress, audio PCM and audio events exactly.

The measured candidate is served at `https://claude-laptop.lan:8188/`.
The served WASM hash matches the timing artifact. Both real game-picker
paths pass gameplay/input/default-policy checks with hardware NVIDIA
rendering, and saved gameplay screenshots were inspected. The existing
browser audio failure remains (audio clock does not advance); full
live-audio E2E is not claimed. Deterministic PCM replays pass.

The public listener serves the frozen artifact independently of builds
and benchmark servers. Automatic restart is enabled with a three-second
delay; applying that policy preserved the running PID. HTTPS returned
200 on the LAN address. This is the existing transient user service,
not a new boot-persistent installation.

At the fixed 3.6 GHz clock, the candidate wall speeds are
Snakes **2.08×** realtime and
Sky Force combat **0.74×**.
These are 18 guest seconds divided by measured elapsed wall time.

Raw builds, plans, commands, all observations, profiles, tests and the full
reproducible source patch are archived under
`/home/claude/.scratch/eka-svc-return-continuation`. Source and artifact
hashes are frozen independently of later documentation changes.
