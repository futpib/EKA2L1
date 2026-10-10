# Whole-game call fusion census

Measured all executed guest BL/BLX calls on the emulation worker during four
reviewed gameplay captures, including ARM, Thumb, interpreter fallback and
ROM/OS code. The pooled results cover **128,092,275 Snakes calls** and
**26,254,725 Sky Force calls**. This measures the retained runtime (`a290e3a68`),
including its existing leaf/Thumb fusion and eight-target indirect-call fusion.
The archived constant-time target-table experiment is not enabled.

| # | Game | Complete call and return fused | Partial fusion, later dispatch | Ordinary call dispatch |
| ---: | --- | ---: | ---: | ---: |
| 1 | Snakes | 56.78% (72,733,400) | 15.77% (20,193,797) | 27.45% (35,165,078) |
| 2 | Sky Force | 5.87% (1,541,243) | 76.13% (19,987,725) | 18.00% (4,725,757) |

These are executed-call fractions, **not CPU-time percentages**, instruction
coverage, or a measurement of every level in either game. Each row pools two
independent launches' observed calls; it is not an unweighted mean of window
percentages. The narrower claim is that a later dispatcher handoff remains for
43.22% of counted Snakes calls and 94.13% of counted Sky Force calls, even though
many of those calls already benefit from partial fusion.

## What the categories mean

- **Complete:** a direct ARM leaf or guarded indirect ARM callee executes inside
  its caller and reaches its matching fused return/continuation. The counter
  records actual successful returns, not just acceptance of an inline target.
- **Partial:** a Thumb callee entry is appended to the current region but its
  return still dispatches; only a veneer/tail prefix is inlined; or an ARM body
  starts fused but exits before its matching inlined return. Same-module sibling
  calls have their own counters and would be included here; none occurred.
- **Ordinary:** an executed call directly returns to the dispatcher, including
  indirect-target misses and calls executed by the interpreter. It excludes
  the later dispatches of calls already categorized as partial.

The denominator counts one call at an executed ARM BL/BLX or Thumb call suffix /
BLX register instruction. Failed ARM conditions and the first half of a legacy
Thumb BL are not calls. SVC instructions, function returns, tail branches and
ordinary intra-region branches are not additional calls in this denominator.
Counters distinguish these calls from the separate total of compiled-region
entries/returns. This does not recognize hand-written call sequences without
BL/BLX as separate calls.

## Which fusion is doing the work

Snakes completes 72,733,400 direct leaf calls inside their callers. There are
**zero executions of the newly retained guarded indirect-call fusion** in its
accepted windows. Its full-fusion coverage comes from earlier direct-leaf work.

Sky Force completes 108,720 direct leaf calls and 1,432,523 guarded indirect
calls. The latter are **5.46% of all counted calls**. Another 31,693 indirect
entries exit before the matching fused return; they are counted as partial,
without assuming an exit reason. The full-body return success rate for those
indirect entries is 97.84%.

The largest partial category in Sky Force is **11,075,847 Thumb callee-entry
fusions**. It also executes 8,880,185 veneer/prefix calls. Snakes has 423,105
Thumb entry fusions and 19,770,692 veneer/prefix calls. An inlined entry or
veneer therefore must not be reported as removal of the complete call/return
handoff sequence.

| # | Separate event count | Snakes | Sky Force |
| ---: | --- | ---: | ---: |
| 1 | Compiled-region invocations and returns to runtime | 143,513,597 | 43,901,232 |
| 2 | Additional fused Thumb non-call branch edges | 2,874,949 | 5,568,590 |
| 3 | Calls executed in the interpreter, included under ordinary | 7,592 | 6,949 |

Region returns include calls, returns, traps, region ends and other exits. Their
count is not the complement of the fused-call count, nor an estimate of time
spent in dispatch. The non-call branch counter covers the existing recursive
Thumb region-fusion mechanism; it does not count every branch already contained
inside a translated region. Per-callsite counts and the thirty most frequent
calls per game are retained in the [JSON report](FUSION_CENSUS_RESULTS.json).

## Windows and scene validation

| # | Game | Guest-second window | Counted calls | Complete | Partial | Ordinary |
| ---: | --- | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes | 74–92 | 76,037,720 | 56.78% | 15.77% | 27.45% |
| 2 | Snakes | 76–88 | 52,054,555 | 56.79% | 15.75% | 27.46% |
| 3 | Sky Force | 58–70 | 10,944,763 | 7.44% | 73.96% | 18.60% |
| 4 | Sky Force | 60–72 | 15,309,962 | 4.75% | 77.68% | 17.57% |

Both Snakes captures show active level gameplay at score 200 at both endpoints.
Sky Force progresses from score 25 / stage 0% to score 1,400 / stage 5% in the
first accepted capture, and from score 825 / stage 4% to score 2,200 / stage 9%
in the second; both retain three lives. These are independently launched games
with overlapping windows, not contiguous segments of one deterministic replay.
The variation in Sky Force demonstrates why one percentage is not universal.

The attempted Sky Force 74–92 window ended on Game Over, and the Snakes 92–110
window ended in a death/transition scene. Both are excluded. Their paths and
screenshot/census hashes remain in the report. No menu or loading counts enter
the accepted windows.

## Measurement and verification

The diagnostic compiler emits constant-address 64-bit counter increments at
actual call/return paths. No per-call JS crossing, hash lookup or imported WASM
helper is added to compiled execution. Counter records are allocated at compile
time; interpreter calls use the diagnostic map. One emulation worker writes the
counts. Reset and snapshots occur while that worker is paused, with registry
locking against concurrent compilation. Compiled-region entries and returns
balance in every accepted capture.

Instrumentation changes native code layout and timing, and can change how much
polling happens during a guest-time window. Runs use the ordinary 2 ms watchdog,
NVIDIA hardware graphics and unpaced execution; they are **diagnostic coverage
runs, not fixed-frequency throughput comparisons**. No speedup or instruction
reduction is inferred from these counts. The regular compiler policies, memory
backend and fusion limits are preserved.

The diagnostic build passes all 182 compiler tests, including 2,880 stack-return
state/memory/callback checks and 240 independent interpreter comparisons.
A browser fixture independently verifies 1,600 known tile calls as:

- 1,600 ordinary calls with fusion disabled, with 4,800 region invocations;
- 1,600 fused entries and 1,600 completed returns with fusion enabled, with 100
  region invocations;
- 1,600 target misses each for the additive and subtractive fixtures outside the
  retained target set, with 4,800 invocations each.

The fixture verifies pixel output as well as the count totals. The retained
counter-check driver passes again against a completed frozen test build. One
intermediate attempt copied test artifacts while their source build directory
was being restored, mixing loader/binary versions; startup failed before any
measurement. It is recorded as a tooling failure and does not affect the frozen
gameplay build or any accepted counts.

**No instrumentation or runtime/default change is retained in production.**
The diagnostic patch and check/summarization tools are archived; normal source
and build outputs are restored. The LAN service remains on the existing fused
artifact throughout.

## Reproduction

Apply [FUSION_CENSUS.patch](FUSION_CENSUS.patch) to the retained source, then build
`eka2l1_wasm` and `test_aot_wasm`. Freeze complete build directories only after
the build command has exited successfully. The patch also modifies `profile.ts`
to reset/read counters at its existing paused boundaries and marks its output
as diagnostic. The patched harness refuses a build without census exports.

Run `fusion_census/check.mjs TEST_BUILD NEW_OUTPUT` under Node for the controlled
counter check. Run the patched `profile.ts` with the usual game assets and
`EKA2L1_WASM_BUILD_DIR`, `EKA2L1_PROFILE_INPUT`, `EKA2L1_PROFILE_START_US`,
`EKA2L1_BENCHMARK_AOT=5`, `EKA2L1_GPU=hardware`, `EKA2L1_SHARED_AUDIO=1`, and
`EKA2L1_CHROME_TRACE=off`. Arguments are `ASSETS NEW_OUTPUT 1 0 END_US`.
Sky Force also needs its manifest and UID. Exact commands, input text and
asset/build hashes are in the JSON report.

Use `python3 fusion_census/summarize.py CAPTURE/fusion-census.json ...` to pool
accepted captures. Review screenshots before including a capture. Reverse the
patch and rebuild normal artifacts afterward. Do not deploy the diagnostic
build as the normal LAN runtime.

Raw artifacts: `/home/claude/.scratch/eka-whole-fusion-20261010`.
