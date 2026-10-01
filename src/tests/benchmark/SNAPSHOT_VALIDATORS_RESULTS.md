# Generated exact-snapshot validator discriminator

Embedding expected code bytes as constants does not support general integration.
The 228- and 512-byte constant validators lose consistently to equivalent
fixed-size two-buffer validators, including a matched cross-instance call control.
At 512 bytes the constants take 123–127 ms versus 84–87 ms for two buffers.
The 8-byte case benefits, but that is insufficient for a new generated-code path.
No emulator runtime, launcher, served archive or CPU acceptance policy changed.

## Method and correctness

Synthetic byte snapshots only; production original-emitter policy is irrelevant
to this offline byte comparator. The helper exports the actual C++
equal_code_bytes with grouped scanner mode2. Generated functions use identical
four-vector OR/XOR groups and exact 16/8/4/2/1-byte tails, either loading both
buffers or replacing the expected buffer with constants. No overreads, byte
sampling or probabilistic hashes. All functions return exact equality.

55 lengths cover 0–32, vector/tail boundaries through 513 and a 228-byte span.
Each of four batches passes 232,143 checks (928,572 total): every individual
byte mutation at all 16 alignments, restored equality, changed expected-buffer
semantics, simultaneous vector mismatches, both inputs ending exactly at the
WASM memory boundary, and zero/nonzero loop counts. Constants intentionally
ignore changes to the second buffer; their oracle is the embedded snapshot.
These are targeted comparator checks, not a new emulator fault/replay claim.

Each timing cell uses a fresh Chromium153 process on the same host. A and B
reverse the complete cell order. Setup, compilation, first call, 500,000-call
warmup and eight five-million-call rounds are retained separately. The driver
stores a byte through an unknown noise pointer before each comparison, identically
for every variant, to prevent invariant-load hoisting. At runtime that address
is outside both spans; outputs and unmodified inputs are checked. This alias
barrier is benchmark machinery, not claimed production work. No builds/profiles
or other owned performance runs overlap. All 480 rounds from 60 processes remain;
rounds within a cell are not independent browser replications.

## Local generated calls

Median milliseconds for five million comparisons. Production is imported from
another WASM instance, while generated functions are local to the loop driver.
This first panel deliberately cannot attribute all differences to byte lowering.

| Span bytes | a production | a generic | a constant | b production | b generic | b constant |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 32.18 | 6.02 | 3.86 | 30.33 | 6.05 | 5.26 |
| 28 | 38.75 | 9.91 | 10.10 | 43.65 | 10.05 | 10.08 |
| 64 | 36.36 | 23.98 | 23.08 | 36.98 | 18.57 | 25.11 |
| 228 | 62.39 | 40.98 | 62.88 | 69.54 | 41.84 | 63.33 |
| 512 | 93.03 | 68.26 | 123.11 | 93.98 | 69.49 | 124.96 |

## Matched imported-call controls

All three targets now cross an instance boundary through the same imported-call
loop. Generic and constant targets belong to a separate generated instance.
Production retains its real variable-length branches and scanner-mode selection;
fixed-size generic code is completely unrolled. These remain different code
shapes, but the local/imported-call confound in the first panel is controlled.

| Span bytes | cross-a production | cross-a generic_cross | cross-a constant_cross | cross-b production | cross-b generic_cross | cross-b constant_cross |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 31.53 | 21.13 | 20.62 | 30.43 | 21.08 | 19.94 |
| 28 | 38.00 | 23.70 | 23.95 | 38.42 | 23.65 | 26.44 |
| 64 | 36.79 | 25.03 | 27.01 | 36.95 | 27.45 | 27.62 |
| 228 | 61.43 | 57.29 | 64.29 | 62.86 | 57.61 | 64.64 |
| 512 | 96.84 | 83.57 | 123.02 | 94.25 | 86.55 | 126.74 |

Fixed-size two-buffer comparison merits a production-path experiment. Its
isolated lead is not a gameplay gain, and importing a standalone production
comparator does not reproduce the compiler's inlining inside the real cache.
Constants also add code/data materialization; total generated module sizes are
recorded (both validators plus three loops), not mislabelled per-function sizes.
A future production integration must retain mapping generation, address space,
all code/dependency bytes, invalidation and terminal mismatch behavior.

## Reproduction and retained artifacts

```
cmake --build build-wasm --target eka_matched_kernel -j6
python3 src/tests/benchmark/generate_snapshot_validators.py SDK/upstream/bin/wasm-as NEW_MODULES
node --experimental-strip-types src/tests/wasm/snapshot-validators.ts HELPER NEW_MODULES NEW_RESULTS
EKA_SNAPSHOT_REVERSE=1 node --experimental-strip-types src/tests/wasm/snapshot-validators.ts HELPER NEW_MODULES NEW_RESULTS_REVERSED
EKA_SNAPSHOT_CROSS=1 node --experimental-strip-types src/tests/wasm/snapshot-validators.ts HELPER NEW_MODULES NEW_CROSS_RESULTS
EKA_SNAPSHOT_CROSS=1 EKA_SNAPSHOT_REVERSE=1 node --experimental-strip-types src/tests/wasm/snapshot-validators.ts HELPER NEW_MODULES NEW_CROSS_REVERSED
```

Helper, original and expanded harnesses, generated modules/WAT, manifests and logs
are retained under /home/claude/.scratch/eka-benchmark/snapshot-validators-*.
SNAPSHOT_VALIDATORS_EVIDENCE.json includes every sample, browser/CPU identification,
helper/source/module/assembler hashes, compilation and first-execution timings.
The helper build uses the existing matched-kernel fixture directory, although
this new discriminator executes no captured guest instructions or proprietary data.
