# Thumb continuations with static instruction counts

This experiment is **not adopted**. Production sources and the browser build
are restored to the baseline; the LAN service was never changed. Sky Force used
**2.70% more worker CPU** on average and Snakes **1.93% more**. The latter pairs
disagree. The static WASM-count gate works, but these measurements establish no
game-level speedup. The tested implementation is preserved in
[thumb-static-regions.patch](thumb-static-regions.patch).

This follow-up tests a replacement for the runtime-count approach in
[THUMB_CONNECTED_REGION_RESULTS.md](THUMB_CONNECTED_REGION_RESULTS.md).
Every guest instruction count is computed by the translator and emitted as an
integer constant. Generated code gains no count accumulator, edge bias, counter
increment, or runtime profitability measurement.

## What the compiler joins

The compiler can copy one forward Thumb continuation into its predecessor.
Each incoming path has its own emitted tail and known instruction count; paths
with different counts are not merged into a runtime counter. Backedges, loops,
wide instructions and unsupported forms keep their original exits. Ordinary
ARM translation and RAM translation are unchanged.

Eligibility requires bounded translation, cached registers, the direct memory
backend, trusted-code policy 3, and permission to continue after stores (the
ROM callers). Exit-census diagnostics retain the original translator. There is
no address or DLL whitelist and no additional environment variable.

A predecessor and successor each contain at most 32 narrow instructions. An
executed joined path contains at most 32, and at most 64 instruction bodies may
be emitted. These are conservative code-growth limits, not measured optima.

Conceptually, a taken forward edge becomes:

```text
# The integers below are compiler-computed constants.
execute predecessor instructions
if condition:
    if budget <= 14 or stopped or unmasked_interrupt:
        return(target_pc, 14)
    publish predecessor fields the tail will not retain
    load only fields newly needed by the tail
    execute tail instruction 15
    if budget <= 15:
        return(next_pc, 15)
    execute tail instruction 16
    return(exit_pc, 16)
execute original fallthrough path
```

Existing budget checks remain; no new dynamic instruction accounting is used.
The actual emitter also retains the first tail instruction's budget check.
Slow memory callbacks publish and reload precisely the register set of their
original block. Shared values remain in WASM locals across the forward edge.
Opcode bodies reuse the existing translator; this does not reimplement guest
instruction semantics.

## Compiler-time cost gate

The bound compares executed generated WASM operations over the entire original
chain and the candidate. It includes zero/short budgets, untaken branches,
callback paths and failed continuation guards. It takes no credit for removing
the C++ dispatcher/lookup or reusing derived memory setup.

Corresponding opcode bodies and callback transfers cancel. Each state-field
transfer costs three WASM operations. The remaining changes are:

- Omit the dead entry PC load and unused PC-index initialization: minus five.
- Omit current-PC publication on pure instructions: minus two each. Instructions
  with a possible memory helper still publish PC before their opcode body.
- Move PC publication after the budget test: minus two on an exhausted-budget exit.
- Retain continuation budget, stop and IRQ tests: at most 4, 11 and 21 additional
  operations respectively on their failed exits.
- On a successful continuation, remove the four-operation branch return and
  two-operation successor PC-index initialization; avoid three operations for
  each shared loaded field and each shared written field.

The compiler accepts only if every modeled exit is non-increasing and every
exit after successfully entering the continuation is strictly smaller. Equal
cost on a guard failure is allowed. Otherwise it returns the original function
bytes. The bound is tied to these emitter shapes; the independent executed-count
tests check the model against emitted modules.

This is a WASM-operation guarantee within the supported lowering, not a V8
native-instruction or CPU-time guarantee. Total function bytes can grow when a
callee is copied into a predecessor even though executed paths shrink.

## Verification

The focused suite passes **22,626** exact state, memory and progress comparisons,
including an independent DynCom interpreter oracle. It covers all 14 Thumb
conditions and all 16 flag combinations, budgets 0–40 and large unsigned budgets,
forward joins, rejected loops, byte/halfword/word memory, register transfers,
stack transfers, and callbacks changing stop, IRQ, flags or memory state.

The separate executed-count matrix contains 34 fixtures, helper/page/arena
memory paths, stop/IRQ variants, and callbacks changing endian state, registers
or mappings before and after the joined edge. Rejected fixtures must remain
byte-identical; accepted fixtures must actually change. Mapped cases assert
that no memory helper ran.

| # | Executed path comparison | Cases |
|---|---|---:|
| 1 | Fewer generated WASM operations | 673,728 |
| 2 | Identical operation count | 33,264 |
| 3 | More operations | 0 |

The counter is injected only into test modules, never browser timing or deployed
builds. The saved modules reproduce the full matrix byte for byte. The shared
counter also reproduces all 19,680 previous memory-span comparisons unchanged.
It includes generated state transfers, control flow and helper call
sites, but excludes structural block/loop/end/else markers, its own increments,
imported helper bodies and the C++ dispatch/lookup. Known branch, loop and call
fixtures independently check the counter. Helper arguments, order, visible
registers/flags, final state, memory and guest progress also match. The
per-invocation AOT budget field is normalized before final-state comparison;
returned progress and the instruction budget boundaries are checked separately.

Examples below use budget 100 and mapped page memory, over the full chain:

| # | Fixture/path | Before | After | Original / new invocations |
|---|---|---:|---:|---:|
| 1 | Long conditional, taken | 343 | 283 | 2 / 1 |
| 2 | Long conditional, untaken | 289 | 252 | 1 / 1 |
| 3 | Memory on both sides of branch | 477 | 403 | 2 / 1 |
| 4 | Memory tail with loads and store | 518 | 455 | 2 / 1 |
| 5 | Stack-transfer tail | 489 | 423 | 2 / 1 |
| 6 | Multiple-register transfer tail | 558 | 481 | 2 / 1 |

Related suites also pass: memory implementations/publication (816 comparisons),
Thumb transfer spans (2,688), Thumb direct memory (2,310), invariant reads (25,600),
invariant writes (16,896), ARM memory/callbacks (21,146), and loop budgets (24,192).

Both 60-frame Chrome replays match native references exactly: pixels, instruction
counts, guest timestamps, PCM and audio events. Snakes ends at 3,012,361,018
instructions / 23,827,651 guest microseconds; Sky Force ends at 15,827,750,326 /
43,864,773. These correctness replays use SwiftShader.

Offline retranslation of 75 ROM Thumb entries from the existing Chrome profiles
accepts one: `EUser.dll` entry `0x801b94c4`, the largest sampled ROM Thumb entry in
Sky Force. Its generated body grows from 3,848 to 5,038 bytes, with a worst-path
cost bound of zero (successful continuations are strictly smaller). The other
74 sampled entries remain unchanged. This is a sample of old profile entries,
not a fresh profile or a count of every dynamically compiled game region.

## CPU timing

Serial baseline–candidate–candidate–baseline comparisons completed. Both builds
use direct memory, hardware-GPU Chrome and shared audio. CPU windows contain
identical guest work and presentation journals, with profiling, diagnostics and
injected counters disabled. No owned build, test, replay or profiler overlaps
these measurements.

Positive changes below mean slower. All four observations per game are retained.

| # | Game | Baseline worker CPU seconds | Candidate worker CPU seconds | Mean change | Paired changes |
|---|---|---|---|---:|---|
| 1 | Sky Force | 7.911538, 7.907095 | 8.013526, 8.232275 | +2.70% | +1.29%, +4.11% |
| 2 | Snakes | 4.936096, 5.110616 | 5.212919, 5.027326 | +1.93% | +5.61%, -1.63% |

Sky Force's window is 42.000001–48 guest seconds / 2,171,043,925 instructions.
Snakes uses the longer 21–33 second window / 1,972,819,249 instructions.
Asset/input hashes and presentation journals match within each game across all
builds/runs. The restored browser build matches all six baseline files byte for
byte; the LAN service was never repointed or restarted.
The small sample does not establish a precise regression magnitude or identify
its cause. In particular, dead PC assignments counted in WASM may already be
eliminated by V8, and larger connected functions can change native register
allocation and code layout. Those are possible explanations, not measured causes.

The requested restriction to statically accounted, non-growing paths was tested;
no runtime counter was needed. It still did not produce a measured CPU win, so
this experiment is retained for reproduction rather than enabled in production.

## Reproduction

The saved probe modules reproduce the count matrix without changing production:

```sh
node src/tests/wasm/thumb-static-counts.mjs \
  src/tests/benchmark/THUMB_STATIC_REGION_RESULTS.json counts.json
```

To rebuild the experiment on this production source baseline:

```sh
git apply src/tests/benchmark/thumb-static-regions.patch
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j 8
node build-wasm/src/tests/aot/test_aot_wasm.js --thumb-static-only
node build-wasm/src/tests/aot/test_aot_wasm.js --emit-thumb-static > probes.log
node src/tests/wasm/thumb-static-counts.mjs probes.log counts.json
```

The test binary's `--thumb-static-rom ROMPATH PC...` mode compares offline
translations of sampled ROM entries. `compare_memory_builds.py` compares frozen
browser builds and supports `--window-us` for longer fixed guest-work windows.
The companion JSON records fixture summaries, full-matrix hash, build hashes,
replay evidence and all CPU observations. Scratch evidence lives under
`/home/claude/.scratch/eka-thumb-static/`.
