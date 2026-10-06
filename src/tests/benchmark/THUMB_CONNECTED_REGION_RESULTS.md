# Compact connected Thumb regions

A later [static-count continuation experiment](THUMB_STATIC_REGION_RESULTS.md)
removes runtime count accounting and enforces a compiler-time cost bound. It
also did not establish a game-level CPU win; its tested source is preserved.

Neither tested variant is adopted. Production translation and the LAN build
remain at `bf12facd3`. The broad variant measured **4.40% more Sky Force worker
CPU**. Keeping selected shapes measured **1.91% less on average**, but the two
pairs disagree substantially; this does not establish a repeatable improvement.

## Implementation tested

The prototype connects narrow Thumb forward branches and backedges to the
entry inside one generated WASM function. It retains register locals across
these edges instead of returning to the C++ compiled-region runner. This is
generic decoding, with no guest-address or DLL whitelist.

Eligibility requires the current direct-memory backend, cached registers,
unsafe-code policy 3, and a caller that permits continuing after stores (the
ROM translation path). A window contains at most 32 narrow instructions;
calls and unsupported forms end it. This bound was a conservative experiment
limit, not a measured optimum. RAM translations and TLB lowering are unchanged.

A local bias adjusts the lexical instruction index on taken internal edges.
Budget checks and returns use the resulting executed count, including paths
that skip instructions or iterate. Removed handoffs retain their stop and
unmasked-IRQ checks. Slow memory callbacks retain state publication and reload.

The first CPU screen did not earn adoption, so a second variant kept repeated
arithmetic loops and memory-bearing diamonds. It excludes early exits, external
branches, bare spin loops, and small forward branches without the measured
memory-diamond shape. This is a structural filter, not a proof of improvement
for every path or budget. Its loop and diamond parts were measured together;
the screen does not identify their separate game-level effects.

## Executed instruction counts

Each matrix compares complete baseline dispatch chains with connected execution
for equal guest work. Twelve fixtures, two input seeds, twelve budgets and three
memory paths produce 864 comparisons. The paths are helper fallback, mapped page
directory, and direct arena. Mapped scenarios assert that no helper was called.

| # | Variant | Fewer WASM operations | Equal | More |
|---|---|---:|---:|---:|
| 1 | Broad | 206 | 48 | 610 |
| 2 | Selected shapes | 176 | 464 | 224 |

State, guest memory, instruction progress and helper arguments/order match in
every comparison. The injected counter includes generated entry/exit work,
state transfers, branches and private fallback functions. It excludes structural
block/loop/end/else markers, counter instructions, imported helper bodies and
the C++ dispatch/lookup work. Therefore this is not a count of all emulator
instructions, nor a prediction of V8 native instruction counts. Known branch,
loop and call fixtures independently check the counter.

Examples below use budget 100 and the mapped page path. Counts are complete
chains, including their entry and exit work.

| # | Executed path | Baseline WASM operations | Selected variant | Baseline / selected invocations |
|---|---|---:|---:|---:|
| 1 | Arithmetic loop, 50 iterations | 5,400 | 4,017 | 50 / 1 |
| 2 | Arithmetic loop, 3 iterations and return | 344 | 297 | 3 / 1 |
| 3 | Load loop, 4 iterations and return | 972 | 807 | 4 / 1 |
| 4 | Load loop, immediate loop exit | 258 | 270 | 1 / 1 |
| 5 | Two-load diamond, first arm | 323 | 317 | 2 / 1 |
| 6 | Stack/store diamond, first arm | 396 | 381 | 2 / 1 |

The excluded bare spin loop grew from 2,100 to 2,928 operations in the broad
variant despite removing 99 handoffs. The selected variant leaves it unchanged.

Even selected shapes can grow on short budgets and helper paths. For example,
the stack/store diamond's first arm grows from 634 to 740 operations with helper
fallbacks. The broader cached state is published/reloaded at each helper, and
connected execution adds count arithmetic and edge checks. These are measured
instruction costs; the CPU screen does not isolate their individual effects on
V8 execution time. Neither variant satisfies a strict no-increase-on-any-path
instruction-count requirement.

## Correctness and CPU timing

Both variants pass 1,980 focused comparisons covering exact budgets 0–40,
branches, nested diamonds, literal gaps, loops, stores, stack transfers, early
returns, and callbacks changing stop, IRQ, endian/register or budget state.
Baseline-chain state and memory are compared with the candidate; ordinary
execution is additionally checked against the DynCom interpreter. PC alignment
is normalized for the interpreter comparison, as in the existing bounded suite.

The broad build also passes the existing memory-implementation and shared-span
suites and 60-frame Chrome replays for both games against native references:
exact pixels, instruction counts, guest timestamps, PCM and audio events.
Snakes ends at 3,012,361,018 instructions / 23,827,651 guest microseconds; Sky
Force ends at 15,827,750,326 / 43,864,773. These replay checks use SwiftShader.

CPU screens use hardware-GPU Chrome, shared audio, fixed guest work, diagnostics
disabled, and no sampling, tracing or injected counters. No owned build, test,
replay or profiler overlaps timing. Each screen uses baseline–candidate–candidate–
baseline order. All observations are retained. Positive changes mean slower.

| # | Sky Force variant | Baseline worker CPU seconds | Candidate worker CPU seconds | Mean change | Pair changes |
|---|---|---|---|---:|---|
| 1 | Broad | 8.097635, 7.834062 | 7.907813, 8.725447 | +4.40% | −2.34%, +11.38% |
| 2 | Selected shapes | 8.042260, 8.701301 | 8.468446, 7.954825 | −1.91% | +5.30%, −8.58% |

Every observation executes the same 2,171,043,925 guest instructions from
42.000001 to 48 seconds and has the same presentation journal within its screen.
The selected build has these timed-window checks and focused correctness tests;
it did not receive a separate native PCM replay campaign. Neither variant was
deployed. Snakes CPU timing was not pursued because Sky Force, the workload with
substantially more sampled Thumb work, did not establish a repeatable gain.

These small screens on a shared host show large variability. The selected mean
is not sufficient evidence to keep the extra compiler machinery. The production
source and local build were restored; all six browser build files match the
frozen baseline byte for byte. The existing LAN service was not changed.

## Reproduction and retained evidence

[Complete evidence](THUMB_CONNECTED_REGION_RESULTS.json) contains both complete
patches against `bf12facd3`, generated probe modules, the standalone counter,
all count rows, focused-test output, native comparison results, all eight CPU
observations, frozen build hashes, and the timing driver. Local build and browser
artifacts remain in `/home/claude/.scratch/eka-thumb-regions/`.

To reproduce a variant, extract its `patch` string from `variants.broad` or
`variants.selective`, apply it to the baseline, and build `eka2l1_wasm` and
`test_aot_wasm`. The patch adds these focused entry points:

```sh
node build-wasm/src/tests/aot/test_aot_wasm.js --thumb-regions-only
node build-wasm/src/tests/aot/test_aot_wasm.js --emit-thumb-regions > probes.log
```

Extract `count_driver` from the evidence as `count.mjs`, then run
`node count.mjs probes.log counts.json`. CPU screens use the existing
`compare_memory_builds.py` with frozen baseline/candidate builds,
`--games combat --modes 2 --rounds 2` and the repository replay assets.
