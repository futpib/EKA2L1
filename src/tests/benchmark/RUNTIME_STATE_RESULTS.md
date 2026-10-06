# Runtime state traffic after state-transfer pruning

The retained performance change removes work from the emulator's **execution
path on every compiled-region lookup**. The existing trusted lookup specialization
is selected only with verification disabled and executable-byte policy 3. It now
omits the `validation_running` load/branch and publication of `aot_code_begin`
and `aot_code_end`. Code generated under that frozen policy does not read those
guard fields. General, diagnostic and reference-verification paths retain their
checks and publication. Mapping, ASID, dependency lifetime and instruction-budget
contracts are unchanged. There is no new environment option, runtime accounting,
dirty flag or profitability branch.

## Game results

The immediate control is `workspace-build`, with full ARM/Thumb state pruning.
The candidate is `quiet-build`, with the same compiler implementation plus the
runtime cuts above. This isolates the runtime change from the earlier compiler
work. Each game ran control/candidate/candidate/control, one browser at a time,
with normal V8 tiering, direct memory 2, trusted code 3, hotpath 2, IR 17, shared
audio and hardware GPU. Capture mode 1 retains frame readback/hashing. Chrome
sampling, tracing and custom guest counters were disabled. No owned build,
correctness test or profiler overlapped a timing window.

| # | Game | Worker CPU time | Retired native instructions | User cycles | Unpaced throughput |
|---|---|---:|---:|---:|---:|
| 1 | Snakes | -1.52% | -1.85% | -3.48% | +0.50% |
| 2 | Sky Force combat | -4.36% | -1.44% | -0.59% | +4.16% |

Both candidate observations use fewer retired instructions and fewer cycles than
both controls in each game. These are modest observed gains from two observations
per build/game, not precise universal speedup estimates. CPU clock and scheduling
variation affect CPU and wall seconds: Sky Force's larger throughput percentage
must not be read as an equivalent reduction in cycle work. The frame journals
match exactly within each panel; no observations are discarded or normalized.

Snakes measures guest seconds 78–96: 4,242,626,167 instructions and 380
presentations. Sky Force combat measures microseconds 42,000,001–60,000,001:
6,439,908,640 instructions and 576 presentations. Timings exclude warmup.
Linux counters cover the same dominant DedicatedWorker identified by the browser
report, with matching PID/TID/lifetime and full enabled/running time without
multiplexing. Scheduled CPU includes kernel time; hardware events are user-only.
Snapshot boundaries are approximate. Full commands, build hashes, browser
versions, raw measurements and artifact hashes are in the companion JSON.

The LAN service continues serving its pre-existing frozen build. These are
branch-build benchmark results, not a live deployment or live-play FPS claim.

## What did not establish a runtime win

The earlier [state-transfer pruning](STATE_LIVENESS_RESULTS.md) makes generated
functions smaller, but that inventory alone did not predict game performance.
Against the no-pruning control in the longer gameplay windows, full pruning
reduces retired worker instructions only 0.10% in Snakes and 1.35% in Sky Force.
Its cycle reductions are 0.28% and 0.95%; CPU/wall changes have mixed signs.
That is weak evidence for a Snakes runtime benefit by itself.

Forcing the trusted lookup's C++ implementation inline removes its standalone
WASM function and grows the binary by just 51 bytes, but changes Snakes retired
instructions by only -0.009% and cycles by -0.305%. Its apparently better CPU/wall
mean is not convincing evidence of a useful execution reduction. The experiment
is removed; its patch and all four observations remain archived. No second-game
claim is made for it.

Removing guard publication alone has a measurable smaller effect in Snakes:
instructions -0.885%, cycles -0.681%, CPU -0.763%, wall -0.758%. The final change
also removes the frozen-off verifier check. Different panels are not additive
measurements of the individual cuts.

Entry-only pruning and store-only pruning were explored while investigating
compiler overhead. Neither is retained: normal entries and private fallbacks
still receive full load/store pruning. The store-only probe's semantic matrix
passed, but its subsequent C++ relocation assertion expected entry-load removal
and failed; it is not represented as a passing complete test target. All early
21–33-second Snakes panels remain in the JSON, separately from runtime results.

## Profile coverage and larger opportunities

The cost-audit tools now include optional RAM exports, paired by their complete
versioned names rather than address alone. Native lowering supports those targets.
The profile summary separately records ROM, RAM and private budget/memory-fallback
self time; Chrome guest labels now recognize those private functions as well.
Raw names and module identities remain available, and inclusive compiler
categories overlap rather than forming additive buckets.

In the warm Snakes control, RAM exports account for 3.660 seconds of 9.163 seconds
of samples, ROM exports 0.936 seconds and private fallbacks 0.042 seconds.
Ignoring RAM therefore missed most generated-code time. An offline TurboFan audit
of eight hot changed RAM entries finds smaller instruction/memory/stack-operand
inventories in all eight, with no branch/call increases. Those are whole-function
inventories including cold paths, not dynamic hot-path instruction counts or
throughput evidence. Actual game counters remain the decision check.

Larger leads remain visible:

1. Cache lookup consumes roughly 10–11% of the warm worker's sampled span. Reducing
   region handoffs or repeated lookups could address more of it. The older
   [guarded successor prototype](MEMORY_AND_CONNECTED_RESULTS.md) had inconclusive
   timings with exact byte checks; it is not an established win for today's path.
2. The outer execution loop accounts for roughly 20%, mixing interpreter and
   compiled-chain work. It is not correct to label the entire bucket recoverable
   dispatch overhead.
3. The integer division helper at `0x80191968` accounts for roughly 5%. A generic
   semantic lowering could address more than state-transfer deletion, but must
   reproduce clobbered registers, flags, precise guest instruction counts, short
   budgets and exceptional paths. The prior [division census](DIVISION_CENSUS_RESULTS.md)
   rejected whole-register memoization because complete inputs rarely repeated.
   No division shortcut or successor cache is installed here.

## Verification and compiler implementation

The final build passes 4,096 cache lifecycle comparisons, 48 actual lookup/runner
publication checks, both active/inactive reference-verifier checks, 18 code-policy
checks and 816 ARM/Thumb memory comparisons, plus direct-memory entry, ASID,
syscall, callback, exception, permission and observer-lifetime checks.

Both 60-frame browser replays match native references exactly: guest records,
pixels, PCM and audio event traces. Snakes completes 3,012,361,018 instructions;
Sky Force completes 15,827,750,326. This is correctness evidence separate from
all timing panels.

The full pruning pass also retains the compiler implementation improvements:
grouped straight-line dataflow/transfer nodes, 32-bit masks for normal state
layouts, reused scratch arrays, single encoding of each barrier template,
build-time proof of fixed deletion costs, and in-place compaction. These affect
translation work, not the generated guest execution path. They preserve all
12,684 common captured ROM/RAM entries, including normalized private callees.
No generated body changes in that identity comparison.

The expanded 24-fixture state matrix passes 7,680 execution/state/helper
comparisons: 5,440 reduce work and 2,240 are unchanged. Final production ARM/Thumb
memory probes pass 19,680 comparisons: 15,432 reduce work, 4,248 are unchanged,
and no operation class grows. The six new fixtures cover grouped barriers,
read-before-write dependencies, loop dataflow and both mask widths. The earlier
entry-policy variant passed the complete 167-test hot-path suite; the final build
uses the focused runtime gates above and the full fixture/memory matrices rather
than claiming that earlier full-suite run was repeated unchanged.

The reusable WASM cost-model, native-cost and Chrome-profiler tests pass. No
benchmark flag is added to normal guest execution. The report and JSON preserve
failed experiments, source/build provenance and validation limits.
