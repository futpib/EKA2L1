# Slow32 frequency in Snakes and Sky Force

Direct had zero ordinary 32-bit load-guard failures in both measured gameplay
windows. TLB failed 3.235699% of these checks in Snakes and 0.256130% in Sky Force.
These are execution counts, not a timing comparison or a claim about all gameplay.

Measured on `wasm-port` at `7181f3350`, with temporary instrumentation in
[SLOW32_CENSUS.patch](SLOW32_CENSUS.patch). Exact counts, inputs, commands, build
hashes and validation are in [SLOW32_CENSUS.json](SLOW32_CENSUS.json).
The instrumentation was removed afterward. Rebuilt normal browser and test
binaries match their pre-census SHA-256 hashes exactly; hashes are in the JSON.

## Ordinary load checks

Here `slow32` means the failure branch of the generated scalar read32 guard:
it either calls the runtime helper or leaves generated code before that instruction
so the interpreter can execute it. A successful direct page-table fallback remains
generated WASM and does not count as slow32.

| # | Game | Memory path | Scalar checks | Slow branches | Failure rate | To helper | To interpreter |
|---|---|---|---:|---:|---:|---:|---:|
| 1 | Snakes | TLB | 75,665,905 | 2,448,321 | 3.235699% | 8,140 | 2,440,181 |
| 2 | Snakes | Direct | 74,930,436 | 0 | 0.000000% | 0 | 0 |
| 3 | Sky Force | TLB | 133,586,857 | 342,156 | 0.256130% | 253,348 | 88,808 |
| 4 | Sky Force | Direct | 133,140,243 | 0 | 0.000000% | 0 | 0 |

The denominator is executions of ordinary scalar guards, including scalarized
block transfers. It excludes loads using an already-proved span, helpers emitted
unconditionally, and reads executed entirely by the interpreter. Counts are path
executions, not distinct guest instructions: retrying an instruction can execute
another guard. They are not hardware page-fault counts.

Snakes covers guest 21-25 seconds: 644,728,231 instructions, 84 presentations.
Sky Force covers guest 42.000001-48 seconds: 2,171,043,925 instructions, 192
presentations. Each game has identical instruction endpoints and byte-identical
presentation journals between implementations. Each row is one fresh-browser
capture, using the existing stock 5320 assets and input replay.

## Other read32 paths

| # | Game | Memory path | Unconditional read32 helpers | Total actual read32 helpers | Whole LDM deferrals | Words in deferred LDMs | Inline 32-bit loads |
|---|---|---|---:|---:|---:|---:|---:|
| 1 | Snakes | TLB | 2,290 | 10,430 | 2,105 | 6,695 | 134,380,395 |
| 2 | Snakes | Direct | 2,290 | 2,290 | 0 | 0 | 136,165,654 |
| 3 | Sky Force | TLB | 2,040 | 255,388 | 2,951 | 8,410 | 669,774,044 |
| 4 | Sky Force | Direct | 2,040 | 2,040 | 0 | 0 | 670,075,590 |

The unconditional helpers are generated sites where the inline memory path was
not emitted, notably short ARM blocks with `EKA2L1_ARM_MEMORY=0`. They are not
failed guard checks. Thus zero direct guard failures does not mean zero helper
calls. Counting helpers alone would miss most of Snakes' TLB fallbacks.

Whole LDM span failures can defer the entire instruction before any transfers.
Those are separate from scalar slow32 branches; a failed span that retries and
succeeds through generated scalar code does not count as a slow exit. Across boot
and gameplay up to each endpoint, direct records 34 whole-LDM deferrals in Snakes
and the cumulative counters for both games are retained in the JSON. A zero
gameplay-window count is not a statement that the path never executes.

The inline-load column includes scalar and proved-span read32 operations. It is
provided as additional activity context, not the denominator of the scalar-check
failure rate. This census does not count byte/halfword memory paths, instruction
fetches, or every C++ MMU lookup.

## Validation and reproduction

Counters are eight 64-bit values in the runtime's primary WASM memory. Generated
code increments them without a JS call; snapshots are taken while the guest is
paused at the measurement endpoints. Actual runtime read32 helper entries are
counted independently. In all four captures:

```
actual32_helper == guard32_helper + unconditional32_helper
guard32_attempts >= guard32_helper + guard32_interpreter
fast32_loads >= guard32_attempts - guard32_helper - guard32_interpreter
```

Sixteen targeted TLB/direct cases validate scalar success, helper fallback,
interpreter deferral, unconditional helpers, and successful/deferred LDM spans.
The instrumented build also passes 471 ARM/Thumb state, memory, budget, crossing
and alias comparisons, plus direct mapping/publication/lifetime checks.
The census has overhead and its recorded CPU/wall times must not be used for
performance comparisons.

Apply the patch to `7181f3350` in an isolated checkout, then run:

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j8
node build-wasm/src/tests/aot/test_aot_wasm.js --slow32-census-only
node build-wasm/src/tests/aot/test_aot_wasm.js --memory-implementations-only
python3 src/tests/benchmark/memory_implementations.py /tmp/eka-slow32-captures timings \
  --build build-wasm/src/emu/wasm \
  --snakes-assets /home/claude/.scratch/eka-benchmark/assets \
  --sky-assets /home/claude/.scratch/eka-sky-performance/assets \
  --reference-root /home/claude/.scratch/eka-chrome-profiling \
  --modes 0 2 --rounds 1
```

The driver uses its `timings` phase only for fixed-work windows and counter
snapshots. `memory_impl_work` contains the endpoint deltas. Sampling, Chrome
tracing, the verifier and the normal detailed guest profiler are disabled.
`EKA2L1_MEMORY_IMPL=0` selects TLB; `2` selects the retained all-cuts direct path.

Restore the source by reversing the patch and rebuild both targets before using
this checkout for any normal run or performance comparison.
