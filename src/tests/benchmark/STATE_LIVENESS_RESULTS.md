# State transfer pruning and V8 cost validation

ARM and Thumb translations now omit cached-state loads whose values are never
used and stores whose fields have not changed. The analysis runs while compiling;
guest execution gains no dirty flags, counters, extra branches or profitability
checks. This follows the [shared WASM ledger](WASM_COST_ACCOUNTING_RESULTS.md).

The cost check now has a reusable native validation stage. It compiles captured
modules with the installed Chromium's TurboFan, extracts the actual x86-64 code,
and compares instructions, memory operands, stack operands, branches and calls.
Compiler work is reported separately from generated guest execution. This caught
a real regression that a generated-code-only model missed.

## Static safety and profitability

`state_local_cache::finish` tags complete, stack-neutral state transfers. The
new pass decodes the emitted body with the shared opcode/immediate specification
and builds a control-flow graph over state uses, definitions, transfers, calls
and structured control. Forward may-dirty analysis removes redundant stores;
backward liveness then removes unused reloads. Joins use union and loop backedges
iterate to a fixed point. Every call invalidates state/local equality, including
bare and outlined calls. Helper publication and reload positions are preserved.

The pass only deletes tagged transfers, relocates both kinds of private call
operand, and retains the original body on unsupported input. Shared epilogues
remain conservative: a field dirty on any incoming path is still published.
No per-path dirty state or duplicated epilogue is introduced. This applies to
valid emulator-owned CPU state, including fields first mentioned after helpers
and fields consumed by derived-memory reloads.

The shared `path_gate.material_improves()` requires non-growing WASM work on all
supplied paths, no increase in any material operation class, and at least one
material reduction. Local moves and constants cannot by themselves establish
that reduction or pay for added memory, arithmetic, branches or calls. State
pruning uses this gate on its deletion-only edits.

This is a conservative filter, not a native-time guarantee. V8 may also fold
arithmetic, remove dead values, combine addressing, or change register allocation.
The static gate remains entirely inside compilation; the native oracle is an
offline validation tool, not something production invokes for every region.

## Actual V8 calibration

`calibrate_v8_costs.mjs` compiles semantic controls with Chromium
153.0.8010.52 / V8 15.3.76.13, with TurboFan forced. All three reduce emitted
WASM operations from three to one while leaving native instruction inventories
unchanged:

| # | Transformation | WASM operations | Native instructions |
|---|---|---:|---:|
| 1 | Remove a local copy | 3 → 1 | 7 → 7 |
| 2 | Fold constant addition | 3 → 1 | 9 → 9 |
| 3 | Remove addition of zero | 3 → 1 | 7 → 7 |

The reusable `compare_v8_costs.mjs` consumes a captured-module comparison and
selects changed common exports by their summed baseline/candidate self samples.
It records module hashes, export indices, engine version, flags, native bytes,
assembly and inventories. `v8-native-cost.mjs` contains the reusable lowering,
inventory and compiler-profile accounting functions.

The native capture uses a separate single-process browser so its own code memory
can be read before exit. It is never used for benchmark timings. Both sides are
forced to TurboFan; normal production tiering is retained in game benchmarks.
No Node-V8 timing result is substituted for Chromium.

Linear disassembly must not count embedded jump-table data as instructions. The
tool recognizes V8's table-base/indirect-jump sequence and verifies every excluded
absolute target points to an instruction boundary before the table. Unexpected
layouts and undecodable instructions fail closed. NOP padding is counted
separately; trap instructions remain instructions. Stack operands are reported
as such, without assuming every stack access is a register spill. Implicit stack
traffic and helper bodies are not included in that operand count.

## Generated game code

The captured Snakes inventories have 9,347 common exported ROM entries, with
9,261 changed and 86 unchanged after private-call normalization. Neither side
has an unmatched entry. Of the changed entries, 7,958 are Thumb. This is a
compiled inventory, not invocation counts. The changed functions account for
589,836 baseline and 564,985 candidate self-sampled microseconds.

The ten hottest sampled changed entries all shrink in native instruction and
memory-instruction inventories; none adds a conditional branch or call. Both
ARM and Thumb are represented. Full values are in the companion JSON.

| # | ROM entry | WASM operations | Native instructions | Memory delta | Stack-memory delta |
|---|---|---:|---:|---:|---:|
| 1 | `0x80191968` ARM | 3316 → 3295 | 2973 → 2918 | -15 | -8 |
| 2 | `0x801a0790` ARM | 38648 → 37334 | 47169 → 44882 | -1673 | -478 |
| 3 | `0x801a064c` ARM | 850 → 814 | 692 → 678 | -16 | -4 |
| 4 | `0x80191e8c` ARM | 174 → 159 | 135 → 130 | -5 | 0 |
| 5 | `0x801957e4` ARM | 3760 → 3565 | 4191 → 3965 | -226 | -142 |
| 6 | `0x80192c40` Thumb | 2733 → 2007 | 1789 → 1367 | -453 | -226 |
| 7 | `0x801a0460` ARM | 37329 → 36099 | 46317 → 44596 | -1316 | -91 |
| 8 | `0x80192734` Thumb | 1586 → 1226 | 1010 → 826 | -200 | -93 |
| 9 | `0x80192c24` Thumb | 786 → 591 | 462 → 372 | -103 | -47 |
| 10 | `0x8019272c` Thumb | 388 → 289 | 176 → 138 | -42 | -18 |

These are whole-function inventories including cold exits, not counts of the
hot path or speedup percentages. Native register allocation can amplify a small
WASM deletion: `0x80191968` removes 21 WASM operations but 55 native instructions;
`0x801a0790` removes 1,314 WASM operations and 2,287 native instructions. The
opposite also occurs in the calibration controls. Fixed opcode cycle weights
would misrepresent both effects.

## The regression the old model missed

The first implementation reduced generated work yet made Snakes worker CPU time
**7.08% worse**, retired instructions **7.40% worse**, and cycles **8.54% worse**.
Sky Force improved in that same campaign. All unfavorable observations and build
hashes are retained in the evidence JSON.

Chrome sampling isolated the missing cost: inclusive time under
`state_local_cache::finish` rose from **11.155 ms to 389.231 ms** in the Snakes
window. The extra **378.076 ms** nearly matches the hardware campaign's
**375.241 ms** CPU increase. Inclusive translation time rose from 67.699 ms to
445.781 ms. Allocation-heavy analysis was running while the game ran; this was
not evidence of a generated-code register-spill regression.

The pass now uses flat branch/predecessor storage and implicit fallthrough,
instead of heap vectors for every graph node. Its decoder uses a constexpr
opcode table, its backward worklist starts in traversal order, and ordinary
branches no longer allocate a labels vector. These compiler changes retain the
same generated fixture modules byte for byte.

`profileCost` reports translation, finalization and allocation inclusively from
the captured worker's raw Chrome samples, alongside generated-function self
time. These categories overlap and must not be added together. Sampling is for
attribution; hardware-counter runs determine total worker costs.

The final compiler's inclusive finalization sample is **100.107 ms**,
versus 389.231 ms for the first implementation and 11.155 ms for baseline.
Total translation is 163.582 ms. This diagnostic run is separate from
timing runs. Final/initial-candidate captures have 9,347 common ROM exports
with **zero changed normalized bodies**; compiler implementation changes preserved
those generated functions.

## Game performance

Each campaign runs baseline/candidate/candidate/baseline serially for each game.
Snakes executes guest time 21–33 seconds, 1,972,819,249 instructions and 254
presentations. Sky Force executes 42–48 seconds, 2,171,043,925 instructions and
192 presentations. Presentation journals match within each campaign.

The configuration uses direct memory, hardware GPU, shared audio and the normal
production policies. No custom guest counters, Chrome sampling, traces, or owned
concurrent build/test runs are active during measurements. Linux counters cover
the dominant DedicatedWorker, with matching PID/TID/lifetime and full enabled
time without multiplexing. Scheduled CPU includes kernel time; hardware
instructions/cycles are user-only. Attachment and snapshot boundaries are
approximate.

The intermediate flat-graph build removed the original Snakes regression:
CPU -1.59%, instructions +1.87%, cycles -0.21%. Sky Force was CPU -7.74%,
instructions -1.25%, cycles -2.99%. One auxiliary browser process exited before
counter attachment in that campaign; its attachment errors are retained and do
not affect the measured guest worker. Final results follow.

Final build against its fresh ABBA control (candidate-minus-baseline means):

| # | Game | Worker CPU | User instructions | User cycles |
|---|---|---:|---:|---:|
| 1 | Snakes | -2.262% | +1.748% | +0.887% |
| 2 | Sky Force | -10.238% | -1.253% | -3.783% |

The Snakes CPU decrease is not a demonstrated reduction in total instruction or
cycle work; frequency/scheduling differences also affect CPU seconds. Native
guest bodies improve, but compiler overhead remains. Sky Force benefits more.
These results support keeping the generic pruning while retaining the native
and whole-worker checks; they do not support a guaranteed-speedup claim.

## Correctness and reproduction

The liveness fixtures cover loops, branch tables, early/shared exits, both arms
of conditionals, helper mutations, bare calls, repeated publications, suffix
reloads, unsupported bytecode and both private-call relocations. The final
18-fixture matrix passes **5,760** execution/state/helper comparisons:
**3,840** reduce work, **1,920** are equal, and no operation class grows.

All 164 real ARM/Thumb memory probes match the first semantic candidate byte
for byte after the compiler-overhead fixes. Against baseline, their matrix
passes **19,680** comparisons: **15,432** reduce executed WASM operations and
**4,248** are unchanged, with no class growth and matching guest state, memory,
progress and helper traces.

The first semantic candidate passed the full AOT hot-path suite (167 tests).
After conservative bare-call invalidation was added, focused ARM memory,
memory-implementation and shared-span suites passed, including callback,
permission, exception, ASID and observer cases. Subsequent graph-storage changes
passed all liveness fixtures and preserved all 164 production probe modules.

Final 60-frame browser replays for both games match native references exactly:
pixels, guest instruction counts, timestamps, PCM and audio event traces.
Snakes completes 3,012,361,018 instructions; Sky Force completes 15,827,750,326.
The LAN service still serves its previous proven build; this change is committed
and verified in the repository with a separately frozen production build.

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm test_state_liveness test_wasm_cost -j 8
node build-wasm/src/tests/aot/test_wasm_cost.js
node build-wasm/src/tests/aot/test_state_liveness.js --emit > liveness.log
node src/tests/wasm/state-liveness.mjs liveness.log liveness.json
node src/tests/wasm/wasm-cost-model.test.mjs
node src/tests/benchmark/v8-native-cost.test.mjs
node src/tests/benchmark/calibrate_v8_costs.mjs calibration
node src/tests/benchmark/compare_compiled_modules.mjs BEFORE_PROFILE AFTER_PROFILE modules.json
node src/tests/benchmark/compare_v8_costs.mjs modules.json native-costs 10
```

Native inspection requires Linux x86-64, `objdump`, `stdbuf`, the repository's
Puppeteer dependency and Chromium at `/usr/lib/chromium/chromium` (override with
`EKA_COST_CHROMIUM`). The game campaign commands, configurations, build hashes,
raw artifact paths and hashes are recorded in `STATE_LIVENESS_RESULTS.json`.
No ROM or game-specific address gate is used by the optimization.
