# Generic ARM span page reuse

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

Not adopted. Sharing the existing scalar page caches with ARM block transfers
and entry-proved spans passed correctness checks, but reduced Snakes throughput
in all four pairs. Sky Force's average improvement did not repeat consistently.
The production compiler and live deployment remain unchanged; no runtime option
was added. The implementation and regression tests are preserved in the
`source_patch` field of [the evidence file](SPAN_PAGE_REUSE_RESULTS.json).

A later [CPU-time follow-up](CPU_TIME_RESULTS.md) found that the Snakes slowdown
also did not repeat consistently across sessions. The original measurements below
are preserved as observed results, not a claim of a universal 8.9% regression.
Neither panel establishes a repeatable general speedup, so adoption remains rejected.

## Implementation

The common `block_transfer_host` emitter first checked the existing read or write
page key, alignment, and whether the complete span stayed within that page. A hit
reused the cached host base. A miss performed the existing full TLB proof and
cached a successful result. Existing helper callbacks invalidated both keys;
failed whole-span proofs retained precise per-access fallback and fault order.

This applied to eligible ARM block/span operations across straight-line code,
conditional paths and loops. It contained no DLL, game, address or loop-pattern
special case. Read/write permissions stayed separate. Thumb emission was outside
this experiment.

## Whole-game measurements

Serial ABBA and BAAB panels, four runs per variant per game, fresh Chromium
processes, identical fixed guest work, hardware Vulkan, shared audio, policy 17,
hotpath 2, Thumb memory 1 and unsafe-code mode 3. Sampling, Chrome tracing, verifier
and custom diagnostics were disabled. No owned build, test or diagnostic process
overlapped these timings. Outside activity on the shared host was uncontrolled.
Every completed observation is retained.

Throughput change is `mean(control seconds) / mean(candidate seconds) - 1`.

| # | Game | Control seconds | Candidate seconds | Throughput change | Candidate faster |
|---|---|---|---|---|---|
| 1 | Snakes | 2.18567, 2.20376, 2.31157, 2.33043 | 2.32333, 2.66020, 2.44358, 2.48220 | **−8.9%** | 0/4 pairs |
| 2 | Sky Force combat | 11.37870, 10.06550, 11.38810, 9.86324 | 10.33980, 10.23890, 10.38780, 10.06540 | +4.1% | 2/4 pairs |

Snakes executed 644,728,231 guest instructions and 84 presentations per window;
Sky Force executed 2,171,043,925 and 192. The Sky Force pair results were +10.0%,
−1.7%, +9.6%, and −2.0%, so its average is not evidence of a repeatable gain.

## Native-code check

Captured the actual generated module containing EUser entry `0x8019d818`
(`f_2149177368`), then compiled that module with Chromium 153 TurboFan and dumped
its native instructions. This avoided treating fewer emitted WASM operations as
proof of less native work.

For the ordinary aligned, permitted block-load path, address calculation through
the first guest load required:

| # | Path | Native instructions |
|---|---|---:|
| 1 | Original full page lookup | 44 |
| 2 | Candidate cache hit | 17 |
| 3 | Candidate cache miss followed by a successful lookup | 54 |

The candidate branch at native offset `0x2bc` jumps to `0x387`, bypassing the TLB
hash and tag/base loads. The whole native function grows from 5,944 to 6,284 bytes.
These are path instruction counts, not cycle counts or whole-loop speedups.
Timing runs used ordinary Chromium tiering, not the forced TurboFan inspection
configuration. Added miss work, cache-state liveness and larger generated code
are costs; this experiment did not isolate each cost's contribution to game time.

## Observed cache reuse

A separate diagnostic run instrumented only the captured EUser function's read-span
cache branch. It recorded **210,093,259 hits and 630,241,159 misses: 25.001% hits**
over boot through 43 guest seconds in Sky Force. These are whole-run counts, not
counts restricted to the timing window, and the instrumented run is excluded from
performance results. The loader checked the entire original module before
substituting the instrumented module.

Thus the 17-instruction path was taken only about one quarter of the time in this
function. Most accesses took the longer 54-instruction miss path. This establishes
that the fast path was not dominant; it does not establish the cause of Snakes'
regression or translate directly into cycle savings.

An initial diagnostic attempt stalled while awaiting counter resets on parked
workers. It was interrupted and replaced by this cumulative-count run; its logs
are retained and its timing is unused.

## Correctness and provenance

- 14,596 new exact state/memory/budget, permission, alignment, page-crossing and
  callback-remapping cases passed. They cover mixed scalar/block reads and writes,
  conditional joins, moving loop bases, aliasing, both TLB layouts and deferred
  memory exits.
- The focused existing suites passed, including 30,976 proved-span comparisons,
  64 block guards, 32 repeated-read guards, 72 displacement checks, callback state,
  CPSR changes and folded-TLB cases.
- All 2,240 native/WASM fault comparisons matched under policy 17: read and write
  remapping, region block spans, wide snapshots, and both entry-budget variants.
- Snakes and Sky Force each exactly matched 60 unique reference frames, guest
  records, instruction totals, PCM and audio events. These replays used
  SwiftShader; throughput measurements used hardware Vulkan.
- The broader legacy suite was stopped during its 520,016-case bounded-execution
  matrix after focused validation had passed. No full-suite pass is claimed.

Both compared builds used frozen source at `f6bc17a18`, the same matching browser
harness, and diagnostics-free production binaries. The concurrent cleanup moved
trunk to `95ba2f4d6`; it is excluded from this comparison. The patch in the evidence
file applies to the frozen baseline. Commands, build hashes, source patch, native
paths, exact replay comparisons and all timings are retained in that file.
Local artifacts are in `/home/claude/.scratch/eka-span-page-reuse/`.
