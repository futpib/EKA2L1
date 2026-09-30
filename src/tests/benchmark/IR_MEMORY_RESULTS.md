# Intermediate memory exits in IR: not promoted

The mixed integer/memory IR implements precise intermediate snapshots, but its
first completed serial gameplay batch does not establish a speedup. Keep both
IR_SEGMENTS and IR_MEMORY disabled by default. Nothing was pushed or deployed;
no live acceptance was attempted for this slower candidate.

| Build | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Corrected default compiler | 13.3618 / 14.5303 | 13.94605 |
| IR with intermediate memory exits | 16.3835 / 15.6843 | 16.03390 |
| Served ADD/ADC compiler | 14.1635 / 13.4354 | 13.79945 |

Order: fixed/memory/served/served/memory/fixed. Candidate mean throughput is
13.0% lower than corrected default and 13.9% lower than served in this batch.
All samples are retained; this small shared-host batch is not a precise causal
regression estimate. There is no confirmation or promotion claim. Each run
executes guest seconds 78–96, 3,975,618,624 instructions and 676 presentations,
with hardware GPU and shared audio. Warmup and timing are serial; no owned
build, test or profiler overlaps them.

The candidate combines integer segments with dynamic memory exits, compared
against the default compiler with those experiments disabled. This is an overall
comparison, not an isolated estimate of the effect of intermediate snapshots.

## Correctness and provenance

- Full WASM package: 142 tests pass, including 4,608 new exact intermediate
  snapshot/effect comparisons and 2,560 inlined-leaf comparisons with ordinary
  and deferred memory, loops and every tested partial budget.
- All 5,472 explicitly rebuilt native fault comparisons match. New modes require
  IR guard selection and check register swaps and multi-access continuation.
- Native CTest: all three targets pass. Frontend: all seven checks pass.
- Checked replay: 1,600 images, guest records and 4,919,249 stereo PCM frames
  match native exactly. The known native-identical movement heuristic is false.

Archive: `/home/claude/.scratch/eka-benchmark/ir-memory-portable-candidate`.
Base `01376662f` plus archived patch. Main WASM SHA-256:
`c9d629ae1b13434e1b12e3807fa265403028794b09607074cb0294ed13baf461`.
Source hashes were checked before timing. A portability edit replacing a
compiler-specific popcount with C++20 std::popcount left application, full-test
and fault WASM binaries and the native fault binary byte-identical to the
accepted archive. The expanded complete test suite is `ir-memory-final-tests.log`.
Diagnostic builds/tests/replay overlapped; their wall times are not performance
measurements. `IR_MEMORY_EVIDENCE.json` records raw timings and artifact hashes.

## What this establishes and what to change next

See `IR_MEMORY_DESIGN.md`. Ordered guards consume precise pre-instruction
snapshots. A failed guard returns before effects; LDM/STM guard the entire span
before a transfer. Snapshot consumers remain live and assignment is parallel.
Addresses may depend on earlier loads. Code aliases, faults, callbacks, short
budgets and unsupported forms retain the existing runner and compiler paths.

Two captured busy regions now have 17 and 10 segments. Their instruction bodies
grow from 19,852 to 27,681 and from 12,083 to 15,484 bytes. Neither coverage nor
correctness implies a performance benefit.

A concrete lowering cost remains: every guarded-host node performs a full TLB
span lookup, whereas the existing emitter reuses read/write page keys and host
bases within a region. Preserve that reuse in the next candidate, keeping every
snapshot and store/code guard. This source difference is not a measured causal
attribution for the entire regression. Wide multiply values still form segment
boundaries; extending them is a subsequent structural option, not a promised gain.
