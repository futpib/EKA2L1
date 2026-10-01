# Direct-call switches with production code validation

A 32-target direct-call switch does not earn runtime integration from this
experiment. A single-target short fixture benefits, but wider switches are
mixed or slower, and the longer math fixture does not show a repeatable win.
No application runtime, launcher or LAN archive is changed by these tools.

## Controlled comparison

Each module contains 32 identical captured guest kernels and two drivers. The
direct driver selects a target with a WASM branch table and makes a direct call;
the indirect driver uses the same selector with call_indirect. Each driver calls
an imported raw WASM function before every target: it restores the fixture's
input registers/flags and calls the real C++ validated_code_cache::find. There
is no JavaScript bridge inside the measured loops. The exact byte comparator,
address-space check and mapping-generation logic are the current production
implementations. Code-version and lifecycle leases are disabled in this build.

Both module instances exist in every case. Indirect targets belong either to
the driver instance (same) or the other instance (cross). Widths 1, 8 and 32
visit targets cyclically and uniformly. Every cell gets a fresh Chromium153
process, 500,000 warmup calls and eight measured rounds of five million calls.
Batch B reverses the cell order within each kernel. No owned build, game,
profile or other benchmark overlaps the timed batches. No outliers are removed.

## Results

Median milliseconds per five million calls. The eight rounds in a cell share
one process and are not eight independent browser replications. All288 rounds
from36 fresh processes are retained in VALIDATED_LAYOUT_EVIDENCE.json.

| Kernel | Targets | A direct | A same | A cross | B direct | B same | B cross |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Math | 1 | 750.88 | 735.66 | 744.16 | 737.36 | 732.49 | 736.03 |
| Math | 8 | 836.61 | 848.84 | 853.98 | 876.16 | 864.46 | 869.96 |
| Math | 32 | 1298.56 | 1232.89 | 1319.69 | 1357.94 | 1254.84 | 1260.77 |
| Prefix | 1 | 171.91 | 184.35 | 182.07 | 172.30 | 187.27 | 182.87 |
| Prefix | 8 | 201.46 | 195.95 | 201.16 | 196.20 | 198.34 | 205.57 |
| Prefix | 32 | 284.19 | 241.61 | 238.56 | 276.05 | 230.49 | 242.80 |

The single-target prefix improves throughput by about7.2% and8.7% against
same-instance indirect calls (5.9% and6.1% against cross-instance calls). The
single-target math case is slower in both batches. At32 targets the direct
switch loses to same-instance indirect calls for both kernels in both batches.
At8 targets the direct/same comparison changes sign for both kernels. This
supports investigating narrowly selected direct edges only if real workload
coverage warrants it; it does not support blindly clustering many functions.

## Verification and limits

All55,872 native fixture comparisons in the two timed batches pass: seeded
partial budgets, all32 cloned targets at full budgets, and each driver. An
additional check-only run passes27,936 comparisons. Each of the36 timed
processes rejects five mutations: changed primary code, identical bytes at a
new backing pointer, unmapping, explicit invalidation, and address-space change.
Rejected calls leave guest memory unchanged and PC at entry. Zero-call loops
perform no validation; nonempty stable batches resolve their mapping exactly
once, while exact bytes are checked on every call. Total rejection checks:180
in timed runs, plus90 in check-only runs. These are fixture tests, not a rerun
of the emulator's complete fault matrix or whole-game replay.

All clones deliberately share one guest PC and code snapshot; the input
registers are reset for each call. The captured kernel WASM comes from the
older cpu-kernels-base fixture archive, checked against its independent native
fixtures, not a new extraction of the served compiler. There are no leaf-code
dependencies in these blocks, no real successor selection, no cache collisions,
and no scheduler, interrupt or512-block chain handling in the driver. Arbitrary
memory faults are outside this ordinary-memory experiment. Reset cost and
validation are common to both layouts. A real cluster would still need all
existing exit, dependency, budget and invalidation contracts and game testing.

## Reproduction

Configure the optional matched-kernel target with EKA_MATCHED_KERNEL_FIXTURES
pointing to the existing captured .arm files/native-fixtures.json. Build:

```sh
cmake --build build-wasm --target eka_matched_kernel -j4
python3 src/tests/benchmark/validated_layout_probe.py KERNELS NEW_MODULES --bin SDK/upstream/bin
node --experimental-strip-types src/tests/wasm/validated-layout.ts HELPER_BUILD NEW_MODULES NEW_RESULTS_A
EKA_LAYOUT_REVERSE=1 node --experimental-strip-types src/tests/wasm/validated-layout.ts HELPER_BUILD NEW_MODULES NEW_RESULTS_B
```

EKA_LAYOUT_CHECK_ONLY=1 runs the checks without timing. Evidence records module,
helper, fixture and source hashes. Local archives are validated-layout-helper,
validated-layout-modules and validated-layout-{check,timing-a,timing-b} under
/home/claude/.scratch/eka-benchmark. Captured guest bytes remain outside git.

The next diagnostic counts exact validation span sizes before changing scanning
or dispatch. The live folded-TLB build remains unchanged; optimization continues.
