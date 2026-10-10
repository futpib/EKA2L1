# Fusing indirect ARM calls and their returns

This extends the [stack-return experiment](STACK_RETURN_RESULTS.md) to indirect
ARM calls and keeps caller/callee register state in shared WASM locals. The
final fixed-work comparison of Sky Force's captured tile-row renderer improves
CPU throughput by **34.29%**, with **16.02% fewer retired native instructions**.
These are renderer measurements, not whole-game speedups.

**Decision: retain and enable by default.** The evidence for retention is the
repeatable CPU gain on a real, frequently called renderer with identical work,
plus passing correctness/gameplay checks. The short whole-game screen supports
no precise speedup claim. This supersedes the earlier experiment's decision
for this narrower indirect-call implementation; the direct-call prototype
remains archived.

## Why this reaches a different opportunity

A temporary randomized dispatcher-edge sampler in stock Sky Force gameplay
recorded 12,025 edges. The tile caller at `0x70020df8`, tile function at
`0x700032bc`, and continuation at `0x70020e8c` account for 3,666 of them:
**30.49% of sampled handoffs**, not 30.49% of CPU time. The previously targeted
full-image converter at `0x700042fc` had no sampled incoming edge in this window.
The sampler is absent from the measured and production candidates.

The generic compiler uses literal-loaded addresses as candidate function
targets. It checks the actual live register at each selected `BLX`, so a changed
function pointer, an unrelated caller, or a Thumb target still takes the normal
path. No game address, name, or instruction sequence appears in the compiler.

```text
stock:
    publish caller registers; PC = target; return to C++ dispatcher
    lookup target; indirect WASM call
    callee: push saved registers and LR; execute; pop registers and PC
    publish callee registers; return to C++ dispatcher
    lookup continuation; indirect WASM call; reload registers

fused:
    if live_target == candidate:
        LR = continuation                  # shared WASM local
        execute inlined push, body, and pop # ordinary guest memory semantics
        if restored_PC != continuation: exit_normally
        execute caller continuation        # same locals, no call or state copy
    else:
        perform normal BLX and dispatcher exit
```

There is no JS crossing in the successful fused path. Helpers, faults, pending
interrupts, and watchdog exits retain their observable state publication.
Stores may alias the saved return address, so storing callees retain the return
target check. Read-only callees can prove that target. Stack pushes and pops are
still real guest memory operations; this change does not remove them.

## Fixed-work evidence

Each measured row executes 100,000 complete captured tile-row calls, rendering
16 tiles per call. The fixture contains the actual caller's selection branches,
eight candidate callees and the actual 61-instruction hot callee. It uses the
real compiler, compiled runner and direct-memory arena, and verifies all output
pixels. Guest inputs and work are identical in both variants.

| # | Measurement | Stock | Fused |
|---|---|---:|---:|
| 1 | Mean worker CPU seconds | 0.733938 | 0.546524 |
| 2 | Mean retired native instructions | 6,103,572,455 | 5,126,064,373 |
| 3 | Faster adjacent CPU pairs | — | 4/4 |

Chrome 153 / V8 15.3, ordinary tiering, ABBA/BAAB, isolated CPU 7 and sibling
15, requested 2.4 GHz; measured 2394.38–2394.42 MHz. Hardware counters ran for
100% of each interval. All rows, including the slower first control, are kept.
Debugger attachment and module capture happen after timing. Compilation is
intended to finish before the timing windows. Host settings were restored.

The later [target-table investigation](INDIRECT_TARGET_TABLE_RESULTS.md) found
that this driver's short warmup did not guarantee completed V8 tiering. Its
corrected single-build off/on verification of the retained eight-target fusion
measures **+36.57% CPU throughput and -15.99% native instructions**, with both
root TurboFan installations verified before timing. This confirms the retained
renderer benefit; the older 34.29% figure above remains a historical observation
whose original tiering exclusion was not verified.

The fixture uses the registry's ROM lookup path for its captured game code;
the game normally uses RAM lookup. It establishes a runtime improvement for
this fixed renderer workload, not the exact cost of the game's dispatcher.
An earlier small-caller fixture improved 21.07%; the broader first candidate's
full-row fixture improved 32.00%. The final comparison above supersedes those
exploratory compiler versions.

## Gameplay screen

The final candidate versus stock uses Sky Force at guest seconds 58–62, ABBA,
the same clock/isolation controls and diagnostics-free runner. All four clock
checks pass; both adjacent CPU pairs improve. Mean worker CPU time falls from
0.859935 to 0.845707 seconds (**+1.68% observed throughput**); retired native
instructions fall 1.26%. Endpoint screenshots show actual gameplay, score 0,
stage 0%, but slightly different background and enemy positions. Presentations
are 128, 127, 128, 128. This is not identical work and does **not** establish a
1.68% whole-game gain. The earlier broader candidate's gameplay mean was flat.

The attempted Snakes timing window ended in its Start Game menu and was stopped
before running a candidate. A second route probe reached the level intro. No
Snakes gameplay throughput claim is made from those runs.

The count-free unpaced clock advances on watchdog preemption or idle. A faster
renderer can potentially leave more polling work before the next clock advance;
that is a possible reason a renderer gain does not appear proportionally in
guest-time windows. This experiment has not measured that causal explanation.
The fixed-work result, rather than a noisy game-window percentage, establishes
that retaining state across this frequently executed boundary pays off.

## Correctness and limits

The final build passes all 182 compiler tests, including 2,880 full-state,
memory and callback comparisons, 240 independent interpreter comparisons, and
12 generated proof-fallback cases. Tests cover both memory backends, helper
and direct paths, aliased saved returns, changed and odd indirect targets,
missing data/stack mappings, flags, interrupts and callback mutations.

Both games pass the served launcher's paced gameplay and keyboard-input checks
on the final artifact, with NVIDIA Vulkan rendering. Reviewed screenshots show
actual Snakes gameplay and Sky Force progressing from score 0/stage 0% to
score 1,350/stage 4%. These checks are muted; live audio was not tested. The
measured runtime is deployed on `https://claude-laptop.lan:8188/`; its downloaded
WASM matches the final candidate hash in the JSON report.

This currently accepts matching ARM push/pop frames with no interior SP/LR
changes, no nested calls, and no branches escaping the body. Internal loops
and conditionals are supported. Target discovery is limited to literal hints
in the caller window and the existing eight-site bound. Caller entry-memory
proofs are disabled when these calls are selected; callee proofs remain usable.
Strict code-write tracking does not select this optimization. Direct-call
expansion from the earlier prototype is excluded from the final candidate.

The focused fixture and driver are `src/tests/aot/test_stack_return.inc`,
`tile_call_image.inc`, and `src/tests/benchmark/stack_returns/micro.mjs`.
Raw artifacts, builds, attempted tests and measurements are retained under
`/home/claude/.scratch/eka-fused-calls-20261010`.

## Reproduction

The compiler policy `arm_indirect_calls` is selected before translation. The
test fixture builds both variants in one process; there is no browser setting
or per-dispatch policy check. From the repository root, with the existing
Emscripten build configured:

```sh
cmake --build build-wasm --target test_aot_wasm eka2l1_wasm --parallel 6
node build-wasm/src/tests/aot/test_aot_wasm.js --stack-returns-only
node build-wasm/src/tests/aot/test_aot_wasm.js
task_root=$(mktemp -d /tmp/eka-fused-calls.XXXXXX)
sudo -n systemd-run --quiet --scope --slice=ekabench.slice \
  python3 src/tests/benchmark/fixed_frequency.py --khz 2400000 \
  --state "$task_root/host.json" --platform-profile performance \
  --isolate-cpus 7,15 -- \
  node src/tests/benchmark/stack_returns/micro.mjs \
  build-wasm/src/tests/aot "$task_root/rows" 7 2400 2304 \
  tile_row_benchmark 100000
```

CPU IDs and reference frequency above describe the measured laptop; use the
appropriate topology and hardware reference frequency on another machine.
The driver rejects wrong measured clocks and partially running counters.
The [machine-readable report](FUSED_INDIRECT_CALL_RESULTS.json) retains all
final observations, hashes and host-restoration results.
