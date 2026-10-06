# Compact dispatch and division lowering

The retained runtime change gives trusted RAM dispatch a contiguous cache of
compiled functions. It removes the version-object pointer chase and metadata
loads from hits. The generic division-digit experiment is **removed**: substantial
WASM savings did not establish a useful whole-game runtime gain. Commands,
hashes, every observation and replay results are in the [evidence](DISPATCH_AND_DIVISION_RESULTS.json).

## Retained dispatch result

Control: `ff8954d5d`, frozen `eka-state-speed/quiet-build`. Candidate:
`eka-dispatch-division/dispatch-build`, SHA-256
`0b15bda39a8da57a0ad8cbfec4d0348e44ec7d85a722fe5804582ec866076129`.

Each game ran control/candidate/candidate/control, with one fresh browser at a
time and normal V8 tiering. Negative CPU/instruction/cycle deltas mean less work;
positive throughput means faster execution.

| # | Game | Worker CPU time | Retired native instructions | User cycles | Unpaced throughput |
|---|---|---:|---:|---:|---:|
| 1 | Snakes | -5.25% | -6.62% | -6.27% | +4.79% |
| 2 | Sky Force combat | +4.22% | -0.99% | -0.31% | -3.86% |

Snakes has a consistent native-work reduction: both candidates retire fewer
instructions and use fewer cycles than both controls. Sky Force's first panel
has almost neutral cycles but worse CPU/wall seconds, alongside a lower effective
clock. It is not evidence of a Sky Force speedup.

A predeclared reversed-order follow-up checks that ambiguity. Its first candidate
is the final control of the division panel, followed by two original controls
and one cache candidate. The shared observation is explicitly identified in the
JSON and must not be counted twice as independent evidence.

| # | Game | Worker CPU time | Retired native instructions | User cycles | Unpaced throughput |
|---|---|---:|---:|---:|---:|
| 1 | Sky Force confirmation | +1.71% | -0.99% | -1.74% | -1.62% |

All runs remain in the evidence. Clock/scheduling drift makes small changes in
seconds uncertain; instruction reductions alone do not guarantee speed. These
measurements establish the useful Snakes result and bound the weaker Sky Force
case, rather than claiming an equivalent win in both games.

## Dispatch contract

Only the existing verifier-disabled, diagnostics-free, trusted-byte specialization
uses the table. Each of 4,096 slots contains a complete ASID/PC/mode key, mapping
source, nonzero mapping generation and function. Hits still compare that identity.
Cold misses use the original resolver and primary/dependency backing and extent
checks. The table costs approximately 96 KiB in the WASM build.

Insertion/replacement, function attachment, byte/mapping invalidation and explicit
invalidation evict matching slots. Failed refreshes and generation-zero lookups
also evict the prior dispatch entry before validation, preventing stale reuse
when a generation is reset. The tests caught that requirement during development.
No live-version pointer is dereferenced by a dispatch-table hit. Cache reset
recreates the table. Normal byte-checking and reference-verification paths retain
their original behavior. There is no new runtime option, instruction accounting,
region limit or generated-code change. ARM and Thumb share this lookup.

## Rejected division experiment

The compiler recognized consecutive ARM `RSBS scratch,D,N,LSR #shift`,
`SUBCS N,N,D,LSL #shift`, `ADC Q,Q,Q` triplets, with descending shifts and distinct
non-PC registers. Four or more triplets had to fit inside an existing budget
proof with no interior entry. The last triplet remained unchanged to reproduce
scratch and flags; short budgets used the existing precise fallback.

For the preceding `k` digits ending at shift `L`, it emitted:

```text
bits = D ? min((N >> L) / D, (1 << k) - 1) : (1 << k) - 1
N -= (bits * D) << L
Q = (Q << k) | bits
charge exactly 3*k guest instructions
```

Shifting the numerator before division avoids overflowing the divisor shift.
Zero divisors produce all-one quotient bits without trapping. This matches
instruction semantics, with no ROM address, game identifier, ABI shortcut,
result memoization or dynamic count reconstruction in the optimization.
The shared WASM ledger compared a conservative 13-operation floor per removed
digit with the replacement's longest 33-operation path. It proved a WASM count
reduction, not the latency of native division.

The synthetic matrix passed 5,376 interpreter state/budget comparisons across
28 register/shift/final-ADCS fixtures, plus rejection checks. Both games passed
exact 60-frame native-reference replays. On the captured division helper,
3,328 complete-state before/after comparisons passed: 170 reduced executed WASM
operations and 3,158 were unchanged, with no growth. The frequently observed
`0x40000000 / 0x101e` call still executes 88 ARM instructions while WASM operations
fall from **2,590 to 1,934** (25.3%).

The captured inventory has 13,949 common ROM/RAM entries; 41 ARM entries change.
The hot helper's whole-function TurboFan inventory falls from 2,918 to 2,806
native instructions, with 40 fewer memory/stack operands and four fewer
conditional branches. That inventory includes cold paths. Other changed entries
have mixed memory/call inventories; this is not a blanket native-path proof.

The incremental game panel compares dispatch-only against dispatch plus division:

| # | Game | Worker CPU time | Retired native instructions | User cycles | Unpaced throughput |
|---|---|---:|---:|---:|---:|
| 1 | Snakes | +17.23% | -0.36% | +5.02% | -17.19% |
| 2 | Sky Force combat | -1.03% | +0.00% | +2.91% | +0.92% |

Snakes retires only about 0.36% fewer native instructions. Cycle observations are
mixed and the mean worsens; Sky Force has essentially unchanged native work.
One slow Snakes candidate is retained in the averages, not discarded as noise.
The evidence does not justify keeping the added compiler mechanism. It does not
prove that replacing the full division algorithm would be unhelpful.

The [archived patch](division_digits_experiment.patch) applies to this report's
retained source and restores the implementation and `--division-only` test.
The reusable offline counter is `src/tests/wasm/division-counts.mjs`; the evidence
records its invocation against captured modules. Neither instrumented modules
nor forced-TurboFan capture are used for game timings.

## Verification and measurement limits

Final review added eviction when a general lookup fails refresh while a trusted
slot exists. The final binary is separately frozen, SHA-256
`a39b6350ca37a7cc2a805227017998288cd3f8da96a668d7036c8b7f94daee1a`. Raw function-body comparison finds only
`find_original` and `lookup_trusted_uncached` changed from the timed prototype;
12,782 other C++ WASM bodies, including the hot dispatch callers, are identical.
Two additional runs use this final binary and confirm its guest work and hardware
instruction counts. They are validation observations, not a new balanced speedup
comparison. Their absolute speeds remain sensitive to host clock/scheduling:

| # | Game | Unpaced speed | Presentations/sec | Worker CPU for 18 guest seconds |
|---|---|---:|---:|---:|
| 1 | Snakes | 1.866x | 39.40 | 8.289s |
| 2 | Sky Force combat | 0.558x | 17.85 | 29.871s |

The active-worker cycle/CPU-second ratio in Sky Force is approximately 4.09 GHz
in the first controls and 3.30 GHz in the final validation run. The final absolute
FPS therefore must not be compared with an earlier session as a code-speed delta.

The final binary passes 12,288 cache lifecycle comparisons; late/null attachment, collisions, ARM/Thumb identity,
byte invalidation, reset and absent-generation checks; normal lookup, guard
publication, active reference verification, executable-byte policy and execution
limit tests. The division build additionally passed 24,192 loop-budget comparisons.
Final cache-only browser replays independently match native images, guest records,
PCM and audio events: 3,012,361,018 Snakes instructions and 15,827,750,326 Sky Force
instructions. The LAN service is not changed and nothing is pushed.

Timings use the hardware GPU, direct memory 2, unsafe code 3, hotpath 2, IR 17,
shared audio, and capture mode 1 (frame readback/hash retained). Chrome sampling,
tracing, verification and detailed guest counters are off. Snakes measures guest
seconds 78–96: 4,242,626,167 instructions and 380 presentations. Sky Force measures
42,000,001–60,000,001 microseconds: 6,439,908,640 instructions and 576 presentations.
Frame journals match within each panel. No owned build, test or profiler overlaps
a timing window. Raw hardware counters belong to the matching DedicatedWorker
PID/TID/lifetime, with full enabled/running time and no target-thread errors.
Other transient-thread attachment errors remain in the JSON. CPU time includes
kernel execution; hardware events are user-only and snapshot boundaries are
approximate. No host governor or affinity setting is changed.
