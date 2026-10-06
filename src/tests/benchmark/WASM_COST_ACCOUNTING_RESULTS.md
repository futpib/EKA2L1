# Shared WASM cost accounting and Snakes diagnosis

The compiler and regression tools now share one opcode cost specification.
Profitability decisions remain compiler-time decisions; production games acquire
no instruction-cost counters. The Thumb continuation experiment remains disabled.

The earlier Snakes CPU-time regression did not establish an instruction-work
regression. A fresh hardware-counter campaign found almost unchanged native
instructions (+0.038%) and fewer user cycles (-0.710%), despite more scheduled
CPU time (+1.486%). The operation-class audit also found a real weakness in the
scalar WASM gate: 972 tested paths add a comparison and a control operation while
reducing the total. Neither finding alone identifies the cause of the historical
+2.12% result.

## One model, explicit scope

[`wasm_cost_model.def`](../../emu/cpu/include/cpu/aot/wasm_cost_model.def) defines
opcode classes and immediate encodings once. The C++
[`wasm_cost.h`](../../emu/cpu/include/cpu/aot/wasm_cost.h) ledger and JavaScript
[`wasm-cost-model.mjs`](../wasm/wasm-cost-model.mjs) decoder consume it. Unknown
opcodes fail closed, including when an unsupported cost is subtracted from itself.

Costs retain separate local, constant, load, store, control, call, global,
memory-size, arithmetic and integer-division counts. Each nonstructural opcode
counts as one WASM operation. Block/loop/else/end markers cost zero under the
existing test convention. These are neither native instruction counts nor cycle
weights; an `if` is one control operation, not a claim about native branches.

`path_gate` compares corresponding paths, accepting only known, non-growing paths
with at least one strict reduction. Individual paths can require strict reduction.
`no_added_material_work()` additionally refuses to exchange local/constant savings
for increases in the other classes. This stricter test is available to callers,
not imposed on every existing optimization: the successful direct-memory lowering
also trades some arithmetic for removed control/local work.

```cpp
using namespace eka2l1::arm::aot::wasm_cost;
path_gate gate;
gate.compare(old_taken_cost, new_taken_cost, true);
gate.compare(old_fallthrough_cost, new_fallthrough_cost);
gate.compare(old_budget_exit_cost, new_budget_exit_cost);
gate.compare(old_helper_exit_cost, new_helper_exit_cost);
if (!gate.no_added_material_work()) return original_translation;
```

The caller must establish equivalent guest work, path coverage, and helper/state
semantics. This API does not discover a CFG or prove equivalence. Include setup,
guards, cold exits and fallback paths in the supplied costs. Helper bodies and
the outer C++ dispatcher are outside the generated-code scope; eliminating a
dispatch earns no assumed credit here. Code bytes and local declarations are
reported separately from executed path work.

Actual reuse in this change:

- The existing Thumb multi-access eligibility check uses the shared amortization
  helper, with the same decision for every register count (0 through 16).
- The saved Thumb continuation experiment now uses this ledger for loads/stores,
  entry/exit work and budget/control deltas. Its generated fixture modules are
  unchanged. Apply [the shared-model patch](thumb-transfer-shared-cost.patch) to
  reproduce it; it is not part of the production translator.
- ARM/Thumb memory and Thumb continuation tests use the same decoder and optional
  per-class executed counters. Counters are injected only into test modules.
- Compiled-module inventory uses that decoder too. It normalizes private callee
  indices before comparing functions, so module packing does not look like an
  optimization hit.

## What the scalar gate hid

The complete existing Thumb matrix still reports 86,136 reductions, 600,792 equal
paths and zero total-operation increases. But 972 short-budget paths increase
both arithmetic and control by one. For example, `early_return`, flag 0, seed 3,
budget 2, helper mapping, no pending stop:

| # | Executed WASM class | Before | After |
|---|---|---:|---:|
| 1 | Locals | 63 | 59 |
| 2 | Constants | 10 | 8 |
| 3 | Loads | 9 | 8 |
| 4 | Stores | 6 | 6 |
| 5 | Control | 4 | 5 |
| 6 | Arithmetic/comparisons | 11 | 12 |
| 7 | Total | 103 | 98 |

The extra budget check is counted, but cheaper/dead entry work pays for it in the
scalar total. V8 can already eliminate some of that entry work. Thus the old
accounting was complete for its chosen raw-opcode metric, but that metric hid a
material tradeoff. The strict per-class gate exposes it without inventing cycle
weights or adding runtime accounting. This audit does not prove these cold exits
caused the game slowdown.

The ARM/Thumb memory matrix also passes: 6,986 reduced and 12,694 equal paths, no
total increases, with matching guest state, memory, progress and helper traces.
It has 4,900 arithmetic-growth paths and no load/store/control/call growth. That
is why one shared accounting system should support explicit selection policies,
rather than pretending a single scalar or componentwise rule predicts every CPU.

## How many hits?

The earlier offline sample accepted **1 of 75 distinct ROM Thumb entries**:
1 of 31 sampled Snakes entries and 0 of 46 Sky Force entries (two were shared).
The accepted Snakes entry, `0x801b1ada`, represented 1,055 of 5,644,555 sampled
microseconds, or 0.0187%. The 64 accepted synthetic entry variants were a test
coverage figure, not 64 game hot spots.

Fresh Chrome profiles captured the actual Snakes worker's modules after sampling
and CPU timing stopped. Baseline/candidate contained 9,347/9,308 exported ROM
entries. Among **9,017 common entries**, **127 functions changed**, all Thumb;
8,890 were unchanged after normalizing private calls. Baseline-only 330 and
candidate-only 291 entries are differing compile inventories, not classified hits.

The 127 changed functions had zero baseline self samples and only two candidate
self samples (1,111 microseconds, 0.0174% of that profile). The sampled changed
entries were `0x801a7dd8` and `0x80479c76`. This is presence and sampled time,
not execution or invocation counts. The profile does not show these translations
as a significant Snakes gameplay bottleneck. More sampled time appeared in
unchanged dispatch/cache lookup/RAM code; sampling alone cannot explain why.

The two sampling runs are diagnostic, not part of the timing mean below. Debugger
attachment and module capture happen after measurement because attachment can
change WASM tiering.

## Snakes hardware counters

Four serial runs used the previously frozen baseline/candidate, in ABBA order.
Each executed guest time 21–33 seconds, 1,972,819,249 guest instructions and 254
presentations, with direct memory, hardware GPU and shared audio. Inputs and
presentation journals matched. No Chrome sampling, trace, custom guest counters,
or owned concurrent build/test was active during these windows.

Linux `perf_event_open` counted user instructions/cycles on existing benchmark
threads. The table is the dominant `DedicatedWorker`, matched by PID/TID and
thread lifetime to the browser CPU report. Both events ran for their full enabled
time without multiplexing. Scheduler CPU time includes kernel execution; hardware
events exclude it. Start gating and snapshot/counter boundaries are approximate.

| # | Build | Scheduler CPU seconds | User instructions | User cycles |
|---|---|---:|---:|---:|
| 1 | Baseline | 5.298856365 | 41,431,421,635 | 19,431,029,729 |
| 2 | Candidate | 5.258886723 | 41,444,873,806 | 19,387,732,612 |
| 3 | Candidate | 5.376176397 | 41,449,962,023 | 19,186,192,389 |
| 4 | Baseline | 5.180520311 | 41,432,249,461 | 19,418,776,729 |

Mean candidate changes: CPU time **+1.486%**, instructions **+0.0376%**, cycles
**-0.710%**. User cycles per scheduler CPU second fell **2.164%**, from 3.707 to
3.627 billion. That lower effective cycle rate accounts mathematically for the
time increase despite fewer cycles. It does not isolate frequency scaling, core
placement, thermal effects, kernel time or sampling-boundary differences. No such
cause is claimed proven. There were no hardware counters in the historical runs,
so this cannot retroactively attribute their exact +2.12% result.

For these small changes, CPU seconds alone are insufficient evidence of increased
instruction work. Retain the candidate as an experiment: it is nearly absent
from hot Snakes samples and has not demonstrated a reliable whole-game gain.

## Further opportunities

More conservative state liveness is a useful next candidate: omit entry loads for
fields overwritten before their first read, and publish only fields actually
dirty on each exit. Those remove shared-memory operations without copying tails
or adding a per-path runtime counter. Account for callback publication and cold
exits with this same ledger.

Broader shared memory proofs are another candidate, particularly ordinary Thumb
scalar and byte/halfword accesses. A static proof must amortize its setup on every
admitted path and survive no potentially invalidating helper. Joining across a
memory fast path would require keeping the original region behavior after a slow
helper; simply returning early after a helper is not an equivalent replacement.
This has not been implemented or measured here.

Broadening the cold Thumb join until more entries qualify is not supported by the
profile. Prioritize transformations on hot generated code; use static path costs
to reject regressions, then native counters and matched game work to assess gains.
The previously discussed broader memory proofs and arithmetic/algorithm lowering
remain follow-ups, not completed optimizations.

## Verification and reproduction

Native and Emscripten cost-model tests pass, as do the JS decoder/counter/module
normalization tests. The shared-ledger continuation patch passes 22,954 exact
interpreter comparisons; all 72 generated fixture modules match the previous
candidate byte for byte. All 164 production memory probe modules also match the
previous production build byte for byte. Detailed counters pass 686,928 Thumb and 19,680 memory
path comparisons. Production memory/span suites and both final 60-frame browser
replays pass against native references, including pixels, instruction counts,
guest timestamps, PCM and audio events.

Production WASM was rebuilt and tested separately; its hash differs from the old
baseline. The other five browser artifacts match. The LAN deployment was not
changed, and continuation joining remains absent from production. The evidence
JSON records both builds, test logs, comparisons and raw-artifact hashes.

```sh
node src/tests/wasm/wasm-cost-model.test.mjs
cmake --build build-wasm --target test_wasm_cost test_aot_wasm -j 8
node build-wasm/src/tests/aot/test_wasm_cost.js
node build-wasm/src/tests/aot/test_aot_wasm.js --shared-spans-only
node build-wasm/src/tests/aot/test_aot_wasm.js --memory-implementations-only
node src/tests/wasm/thumb-static-counts.mjs \
  src/tests/benchmark/THUMB_TRANSFER_GATE_RESULTS.json costs.json --breakdown
node src/tests/wasm/span-counts.mjs \
  BASELINE_MEMORY_PROBES CANDIDATE_MEMORY_PROBES memory-costs.json --breakdown
```

For the archived experiment, apply `thumb-transfer-shared-cost.patch`, rebuild
`test_aot_wasm`, then run `--thumb-static-only` and `--emit-thumb-static` as in
the [previous report](THUMB_TRANSFER_GATE_RESULTS.md). Do not apply both patches.

`profile.ts` with `EKA2L1_CAPTURE_MODULES=auto` and Chrome sampling captures the
selected guest worker's real modules after measurement. Compare captures with:

```sh
node src/tests/benchmark/compare_compiled_modules.mjs \
  BASELINE_PROFILE CANDIDATE_PROFILE modules.json
python3 src/tests/benchmark/scheduler_probe.py ASSETS BUILD OUTPUT \
  --hardware-counters --start-us 21000000 --end-us 33000000 --capture-mode 1
```

The exact profile environment and hardware runner are recorded in
[`WASM_COST_ACCOUNTING_RESULTS.json`](WASM_COST_ACCOUNTING_RESULTS.json).
Raw scratch evidence is under `/home/claude/.scratch/eka-accounting/`; the large
count matrices and captured modules are referenced by hash, not committed.
