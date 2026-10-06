# Select Thumb continuations by state transfers

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

This revision is **not enabled**. Sky Force used **2.50% less worker CPU** in
two paired comparisons, but Snakes used **2.12% more** across four pairs,
including a confirmation campaign. Production sources and browser artifacts
are restored to the baseline; the LAN service was never changed. The stricter
gate is preserved in [thumb-transfer-gate.patch](thumb-transfer-gate.patch).

The [shared-accounting follow-up](WASM_COST_ACCOUNTING_RESULTS.md) adds operation
classes, fresh compiled-module coverage and Snakes hardware-counter evidence.
It preserves this experiment's generated code and supplies a replacement patch
using the common cost model; it does not adopt the continuation optimization.

The previous [static-count experiment](THUMB_STATIC_REGION_RESULTS.md) credited
WASM local assignments which V8 could already eliminate, while retaining new
stop/interrupt checks. This revision requires savings in actual shared-state
loads/stores and forbids joins that introduce either cached state fields or
additional stop/interrupt checks. Selection and guest instruction counts are
entirely compiler-time decisions. There is no runtime profitability counter.

## Selection

A candidate must satisfy all of the following:

- Its prefix up to the selected forward branch cannot call a helper. This is
  conservative: even a memory instruction with an ordinarily successful fast
  path rejects that prefix, because its fallback can change stop/IRQ state.
- Every field cached by the successor is already cached by the predecessor.
  The gate rejects new fields even if they are needed only on the taken branch.
- Keeping shared fields in locals eliminates at least one actual state load or
  store. Local moves, constants, dead PC assignments and dispatcher removal earn
  no credit in this selection test.
- The separate static bound still proves no increase in executed WASM operations
  on any modeled exit, and a strict decrease after entering the continuation.
  Rejected candidates return the original function bytes.

The existing direct-memory, ROM, trusted-code, cached-register, bounded and
non-census restrictions remain. There is no guest-address whitelist. One forward
edge may be joined; backedges and loops keep their exits. At most 32 narrow
instructions execute on a joined path, with at most 64 emitted opcode bodies.
These limits are conservative bounds, not measured optima.

The budget check at the edge replaces the first successor check; it is not
added on top of it. Failure branches share one exit. Each instruction's returned
count is an immediate constant computed by the translator. Existing opcode
lowering is reused, including callback state publication and reloads.

```text
# The outer runner checked stop and IRQ before calling this entry.
execute helper-free predecessor
if branch_condition:
    if remaining_budget <= PREDECESSOR_COUNT:
        return(target_pc, PREDECESSOR_COUNT)
    # Shared registers/flags remain in WASM locals.
    # No new cached fields, stop check or IRQ check.
    execute first successor instruction
    if remaining_budget <= PREDECESSOR_COUNT + 1:
        return(next_pc, PREDECESSOR_COUNT + 1)
    ...
execute original fallthrough path
```

`InterpreterMainLoop` checks pending unmasked IRQ and remaining execution work
before the first compiled call. `execute_chain_impl` checks stop, budget and IRQ
before entering each subsequent function. CPU state belongs to the execution
thread; an admitted narrow arithmetic/branch prefix cannot modify those control
fields. A helper could, so prefixes containing possible helpers are excluded.
This is the precise invariant reused to omit the repeated checks, not an
assumption that interrupts never occur. Per-instruction budget exits remain.

The test chain now models the outer runner's entry checks before its first call,
as well as between calls. Historical saved probes retain their old test-entry
contract. This change is recorded in the probes' metadata; counts from the two
experiments should not be compared as the same matrix.

## Correctness and executed instruction counts

The focused suite passes 22,954 comparisons against the old translation and an
independent DynCom interpreter, with 64 accepted entry variants. Dense pure
prefixes must be accepted; dense prefixes containing a possible helper must be
rejected. The suite includes conditional branches, early exits, budgets, flags,
memory and callback state. Related memory implementation, memory span, ARM
memory and loop-budget suites also pass.

The independent WASM counter checks 36 fixtures across mapped/helper memory,
flags, stop/IRQ and callback variants. All guest state, progress, memory and
callback observations match. Per-invocation budget bookkeeping is normalized;
returned progress and budget boundaries are checked separately.

| # | Executed path comparison | Cases |
|---|---|---:|
| 1 | Fewer generated WASM operations | 86,136 |
| 2 | Identical operation count | 600,792 |
| 3 | More operations | 0 |

This counter exists only in test modules. Browser builds and CPU measurements
contain no injected counters or profiler sampling. It counts generated code,
including state transfers and control flow, and excludes helper implementations,
the outer C++ dispatcher and structural WASM markers.

| # | Example, budget 100 | Before | After | Original / new calls |
|---|---|---:|---:|---:|
| 1 | Long conditional, taken | 343 | 262 | 2 / 1 |
| 2 | Long conditional, untaken | 289 | 252 | 1 / 1 |
| 3 | Dense state reuse, taken | 729 | 595 | 2 / 1 |
| 4 | Dense state reuse, untaken | 487 | 448 | 1 / 1 |
| 5 | Memory prefix, rejected | 477 | 477 | 2 / 2 |
| 6 | Dense prefix with callback, rejected | 563 | 563 | 1 / 1 |

Both games' 60-frame browser replays match native references exactly: pixels,
guest instructions and timestamps, PCM and audio events. Snakes ends at
3,012,361,018 instructions / 23,827,651 guest microseconds; Sky Force at
15,827,750,326 / 43,864,773. Correctness replays use SwiftShader; CPU runs use the
hardware GPU.

## Coverage and native CPU checks

Offline retranslation of 75 ROM Thumb entries sampled by earlier Chrome profiles
accepts one, `0x801b1ada`: 25 shared-state transfers removed and no new fields or
prefix callbacks. Its function body grows from 3,542 to 5,852 bytes because the
tail is copied; its static worst-path bound is minus five WASM operations.
The previously accepted EUser entry `0x801b94c4` is now rejected: it has a helper
in the prefix and would add one cached field. The other sampled entries retain
their original code. This sample is not a census of all game translations or a
fresh profile.

The native microbenchmark uses optimized V8, uninstrumented emitted modules,
thread CPU time and a WASM driver. Repetition and indirect region dispatch both
remain in WASM, so it does not manufacture a JS/WASM boundary benefit. Four
alternating pairs are measured per fixture/seed, after warmup; compilation is
excluded. The measured modules are byte-identical to the final probes for all
reported cases. Node's V8 version is recorded in the JSON and is distinct from
the game browser measurement.

Taken-path CPU reductions include nested branches 17.47%, early-return branches
24.50%, long conditional 20.94% and dense reuse 16.07%. These are microbenchmark
results, not game speedups. The dense untaken case measured 3.06% slower, and an
unchanged memory control varied by +7.33% and +0.63%. All observations are saved,
including controls and unfavorable results; this is not a universal CPU-time
guarantee. No-new-fields also does not prove identical native register allocation.

## Game CPU measurements and decision

Baseline and candidate ran serially in ABBA order, with direct memory, hardware
GPU, shared audio and no sampling or diagnostic counters. No owned build, test,
replay or profiler ran concurrently with the timing windows. Guest work,
assets/inputs and presentation journals match. The final comment-only rebuild
matches all six measured browser files byte for byte.

Positive deltas mean slower. All observations are retained, including the
additional Snakes comparison prompted by the mixed initial result.

| # | Game / campaign | Baseline worker CPU seconds | Candidate worker CPU seconds | Mean change | Paired changes |
|---|---|---|---|---:|---|
| 1 | Sky Force | 8.443791, 8.302959 | 8.171777, 8.156268 | -2.50% | -3.22%, -1.77% |
| 2 | Snakes initial | 5.268304, 5.207715 | 5.354753, 5.199520 | +0.75% | +1.64%, -0.16% |
| 3 | Snakes confirmation | 5.191819, 5.179616 | 5.266084, 5.469813 | +3.51% | +1.43%, +5.60% |

Sky Force measures 42.000001–48 guest seconds / 2,171,043,925 instructions.
Snakes measures 21–33 guest seconds / 1,972,819,249 instructions. Its confirmation
uses the same build, settings and workload, with no intervening code changes.

The gate improves selection: it rejects the old EUser case, creates no extra
stop/IRQ work on admitted joins, and selected native fixture paths become faster.
It does **not** establish that every admitted join improves game CPU time. The
Snakes results prevent adopting it as a general default. Larger functions and
changed native live ranges remain possible costs; this campaign does not isolate
the cause of the Snakes regression. Fewer cached fields and fewer WASM operations
are not a proof of fewer native spills, better code layout or less CPU time.

No further heuristic threshold was fitted to these game timings. The production
baseline remains active, with direct memory as its default.

## Reproduction

The saved probe modules reproduce the count matrix and native microbenchmark
without changing production:

```sh
node src/tests/wasm/thumb-static-counts.mjs \
  src/tests/benchmark/THUMB_TRANSFER_GATE_RESULTS.json counts.json
node --no-liftoff --no-wasm-lazy-compilation \
  src/tests/wasm/thumb-static-native.mjs \
  src/tests/benchmark/THUMB_TRANSFER_GATE_RESULTS.json native.json
```

To rebuild the candidate on the recorded source baseline:

```sh
git apply src/tests/benchmark/thumb-transfer-gate.patch
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j 8
node build-wasm/src/tests/aot/test_aot_wasm.js --thumb-static-only
node build-wasm/src/tests/aot/test_aot_wasm.js --emit-thumb-static > probes.log
```

`--thumb-static-rom ROMPATH PC...` retranslates sampled ROM entries offline.
`--thumb-static-rom-ungated` disables only the extra transfer/field/helper filter;
it still enforces the static WASM path bound. These are compiler-test controls,
not runtime game instrumentation or environment options.

The JSON contains all emitted fixture modules, source/build hashes, test logs,
coverage, native CPU observations, replay evidence and all browser CPU rows.
The saved patch reconstructs all five tested implementation files exactly; the
saved probes reproduce the full count matrix byte for byte. The restored build
matches all six baseline browser files byte for byte.
Scratch evidence is under `/home/claude/.scratch/eka-thumb-gate/`. The full executed
count matrix is kept there with its hash recorded in the JSON.
