# Inline immutable ARM ROM veneers into Thumb callers

Adopted on `wasm-port`.
The candidate eliminates a generated-region handoff for bounded Thumb BLX calls
to immutable ARM literal-PC veneers. It preserves the literal as a runtime read.

## Controlled gameplay result

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.0820 → 7.1093 | -0.38% | -0.40% | -0.00% | 3/4 |
| 2 | Sky Force | 24.7970 → 24.0311 | +3.19% | +2.99% | -2.41% | 4/4 |

Control is `6eb59dbc2`, with source metadata disabled in both timing builds.
Four fresh launches per build per game use ABBA then BAAB order. Snakes covers
78–96 guest seconds; Sky Force combat covers 42–60. Guest progress, instruction
counts and presentation journals match. All valid results, including slower
pairs, are retained. Percentages are throughput changes, not time reductions.

There are 16 valid observations and 1 retained invalid attempts.
CPU 7 hosts the guest worker; sibling 15 is reserved. The requested clock is
3.6 GHz, with measured clock/isolation/throttling/counter checks and no temperature
gates or cooldowns. The host, placement, platform profile and charging settings
were restored; all 88 live restoration checks pass.
See [the method](CONTROLLED_BENCHMARKS.md) and [evidence](ROM_LEAF_HANDOFF_RESULTS.json).

## Which handoff disappears

Previously, the Thumb caller returned to the C++ runner, which looked up and
called the ARM veneer. That veneer loaded its destination, returned again, and
the runner looked up the real callee. The successful candidate path performs
the veneer load inside the caller and returns directly to the real destination.
One lookup, indirect call, return and associated state transfer disappear.
These are WASM-to-WASM boundaries on the same worker; there is no per-call JS
round trip. This does not merge the emulator and translations into one module.

The compiler accepts only an immutable ROM instruction matching unconditional
pre-indexed word `LDR PC, [PC, #±immediate]` without writeback. It uses the existing
checked direct-memory path. A failed mapping/endian/page proof returns at the
ARM veneer before executing the load, leaving the original compiled/interpreted
fallback in charge of faults. Changed literal values and mappings remain visible.
Both Thumb call halfwords and the ARM load retain separate instruction counts
and short-budget exits. The call-half stop/IRQ check has no intervening callback
or memory access before the fused veneer. Nonmatching code and TLB mode retain
their existing paths. No experiment selector or runtime counter is added.

## Actual warmed V8 code

Two native captures use the exact normal timing builds, ordinary V8 tiering and
the existing Linux sampler/JIT-code snapshot tools. A scratch harness permits
captures without source maps for this function-level native-shape audit; these
captures do not claim new ARM/C++ source-line attribution. Both recover all 160
selected code versions without snapshot errors or lost samples. Sampling is off
for the throughput measurements.

In the sampled Sky Force caller at `0x804b16c2`, the control stores PC
`0x804b6f30` and returns seven guest instructions. The candidate loads the
literal at `0x804b6f34` directly, stores its value as the next PC, and returns
eight. The native caller grows from 3,200 to 3,584 bytes; the skipped standalone
veneer is another 576-byte TurboFan function plus the runner boundary. Function
sizes are not executed-instruction counts or speedup estimates. The raw native
code hashes, complete disassembly and sampled version identities are preserved.

## Correctness and reproduction

The AOT suite passes 179 tests with zero failures, apart from its documented
pre-existing crash-repro XFAIL and diagnostics-disabled skips. Focused tests pass
34,560 full-state comparisons over both memory backends and budget layouts,
short budgets, changed literals, different host aliases, denied reads, endian
state, ARM/Thumb destinations, stop/IRQ masks and 64-bit remaining counts.
Another 288 existing call-half boundary cases pass. Partial ROM instruction
windows and unsupported veneer encodings retain the original emitted body/path.
Both real 60-frame game replays match reference images, guest progress, audio
PCM and audio events exactly.

Raw builds, plans, commands, observations, profiles and the reproducible candidate
patch are in `/home/claude/.scratch/eka-rom-leaf-handoffs`. Timing source and build
hashes are recorded independently of later documentation changes.
