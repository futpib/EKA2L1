# Indirect-call instance layout discriminator

This tests the proposed same-instance indirect-call opportunity before changing
emulator dispatch. It does not demonstrate a whole-game optimization.

## Controlled difference

The current emitter at da1829d58 (runtime from 18b450f73) extracts the existing
57-instruction math kernel and seven-instruction memory prefix. The explicitly
rebuilt research probe enables deferred memory exits, as in the current game
build. CMP reuse has already been removed. Native Step fixtures from the earlier
CPU investigation remain the independent register/flag/count/memory oracle.

Each generated module contains 32 identical kernel definitions and one indirect
loop driver. Both layouts use exactly the same module bytes, shared memory,
imported table and register-reset loop. The module is instantiated twice. Table
targets belong either to the driver's instance or to the other instance. Both
instances exist in both cases. A separate direct-call driver is a lower-level
control. No guards are deleted or relaxed.

Target widths are 1, 4, 8 and 32, visited cyclically and uniformly. Each cell uses
a fresh Chromium 153 process, preventing previous cells' call feedback from
contaminating it. It checks all seeded partial-budget states for kernel zero,
all 32 targets at full budgets, and the loop driver against native. Each loop
warms for 500,000 calls, yields one second for background work, then records
eight rounds of five million calls. Batch B reverses the order of cells. Owned
build, gameplay, sampling and other test jobs do not overlap these runs.

## Results

Medians in milliseconds per five million calls; all rounds are retained.
Each batch/cell is one browser process, so its eight rounds are repeated
measurements, not eight independent browser trials.

| Kernel | Targets | A same | A cross | B same | B cross |
| --- | ---: | ---: | ---: | ---: | ---: |
| Math | 1 | 356.55 | 357.47 | 360.40 | 357.55 |
| Math | 4 | 423.76 | 423.85 | 422.06 | 422.91 |
| Math | 8 | 439.96 | 435.57 | 439.84 | 447.81 |
| Math | 32 | 594.64 | 566.06 | 595.45 | 618.16 |
| Prefix | 1 | 121.10 | 129.36 | 124.39 | 120.40 |
| Prefix | 4 | 126.74 | 124.79 | 124.15 | 124.25 |
| Prefix | 8 | 136.85 | 137.21 | 140.03 | 138.39 |
| Prefix | 32 | 146.91 | 146.58 | 148.36 | 147.40 |

For one target, math co-location changes throughput by about +0.26% then -0.79%.
The prefix changes by +6.82% then -3.21%. Wider-target results likewise do not
establish a repeatable co-location advantage. Direct-call controls are roughly
344–345 ms for math and 107–108 ms for the prefix, versus about 357–360 ms and
120–129 ms for one-target indirect calls. That is a kernel-level opportunity,
not a prediction of savings after real successor validation and lookup.

Increasing targets raises math elapsed time substantially in this fixture.
This includes instruction-cache footprint, target prediction and optimization
policy: the targets are duplicate bodies occupying distinct functions. These
measurements cannot attribute the difference solely to inlining or predict the
real game's nonuniform target distribution. No machine-code/inlining trace is
claimed. The two-instance test isolates instance ownership within one compiled
module; actual Emscripten-to-generated dispatch also crosses module boundaries.

All 55,872 native-state comparisons pass across the 36 fresh processes.
The fixture covers seeded ordinary memory and exact short budgets, not arbitrary
faults, code mutation, scheduler behavior or whole-game execution.

## Decision and next experiment

Do not integrate module co-location alone on these results. Direct hot edges or
profile-selected regions can still reduce dispatch and preserve values, but
need real lookup/code-validation integration and whole-game controls. This
supports prioritizing data flow and precise exit design over a broad module
repackaging change.

The next local experiment outlines the precise short-budget fallback into a
private WASM function. The earlier entry-budget trial duplicated both bodies
inside one function; separating them tests a distinct code-size/layout effect.
Its correctness and performance are not established by this report. The served
LAN build is unchanged, and no new live/audio or deployment claim is made.

## Reproduction

Build eka_compiler_probe explicitly (it is excluded from the default build).
Run it with GAME_EXE NEW_KERNEL_DIR --defer-memory, then copy the existing
native-fixtures.json into that new kernel directory. Run:

```sh
python3 src/tests/benchmark/module_layout_probe.py NEW_KERNEL_DIR NEW_MODULE_DIR --bin SDK/upstream/bin
cd src/tests/wasm
node module-layout.ts NEW_MODULE_DIR NEW_RESULTS_A
EKA_LAYOUT_REVERSE=1 node module-layout.ts NEW_MODULE_DIR NEW_RESULTS_B
```

Use absolute paths after changing directory. The manifest records exact module,
probe and fixture hashes, base commit and probe patch. MODULE_LAYOUT_EVIDENCE.json
contains every round and check count. Generated guest code stays in local scratch;
it is not committed. This report adds research tools only.
