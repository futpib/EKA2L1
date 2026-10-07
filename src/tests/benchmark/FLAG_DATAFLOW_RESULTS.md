# Flag and incoming-register data-flow experiments

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The controlled deferred-flags rerun has four observations per variant, in
ABBA then BAAB order, at the original shared-audio Snakes window of guest
seconds 78-96. CPU throughput falls by 1.09% and wall throughput by 1.07%;
all four CPU pairs favor the control (-2.84% to -0.37%). Native instructions
fall by only 0.030%. All eight observations pass the clock and host checks.
The original rejection remains supported for this archived configuration,
although its much larger apparent loss does not repeat. The independent
incoming-register comparison is still running.

The archived flag patch materializes pending recipes before every memory
instruction. The baseline's scalar ARM loads call the memory helpers. Preserving recipes across today's proven inline
memory accesses, with materialization on observable exits and helper paths,
would be a different optimization. These measurements do not test that design
or establish a gain for it. The synthetic-loop results below likewise do not
establish a whole-game benefit.

Baseline: `7219855a2`, the verified shared-audio build. This work implements the
first recommended compiler investigation: deferred flags and avoiding incoming
register loads whose values are overwritten before use. It does not implement
bulk-loop acceleration or asynchronous compilation.

## Deferred carry and overflow

The prototype retains operand/result recipes for unconditional SUBS, ADDS, CMP
and CMN, instead of immediately computing both C and V. Consumers materialize
only the needed flags. Budget returns materialize the pending recipe in their
cold path without consuming the compiler's fallthrough recipe. Conditional
writes, helper boundaries and control-flow joins receive concrete state;
internal taken branches publish their flags before jumping. Exact per-instruction
budgets, code validation and memory/fault semantics remain intact.

The first development version failed a conditional-write differential case;
restricting recipe creation to unconditional producers fixed it. The final
candidate passes the expanded suite and replay below. The two previously
captured hot math/memory kernels contain no eligible ordinary flag-setting
arithmetic, so their timing cannot establish this idea's benefit.

Two synthetic four-instruction loops provide a narrower control: one overwrites
flags before they are consumed, the other consumes carry. The same emitted
modules are instantiated once, with identical state/reset/calling code. After
20,000 warmup calls per variant and a yield for background compilation, eight
alternating rounds each execute 80 million guest instructions. Median times:

| Synthetic loop | Baseline | Deferred flags | Throughput gain |
| --- | ---: | ---: | ---: |
| Flag overwrite | 99.10 ms | 76.57 ms | 29.4% |
| Carry consumer | 83.40 ms | 76.31 ms | 9.3% |

All 792 paired short-budget/state checks pass. The independent interpreter
comparisons are in the main test suite. The initial, shorter-warmup run is also
retained and is mixed; the table uses the longer-warmup repeat. These loops are
not Snakes and do not measure its dispatcher or memory path. No native machine-
code-size or instruction-count claim is inferred from the synthetic timings.

## Incoming-register entry definitions

The second candidate analyzes an unconditional ordinary-ALU prefix at region
entry. A register written before any read does not need its incoming load.
However, a budget return before that definition must leave its original memory
value untouched: barrier offsets identify those earlier exits and omit the
corresponding flush. Later helper reloads remain complete. MOV/MVN also avoid
reading their architecturally unused Rn operand.

The analysis stops at conditionals, control flow, memory and special encodings.
It is deliberately conservative; this is not whole-function register allocation
or general SSA. It removes a load and adjusts earlier exits, unlike the prior
experiment that mostly shrank exit stores. Neither candidate uses game names
or a guest-address whitelist.

## Real Snakes measurement

Each candidate is compared independently against the same archived audio build,
in baseline/candidate/candidate/baseline order. One browser runs at a time,
including warmup; no owned build, replay or sampling job runs during the timing
windows. This is a shared host and no separate host-load investigation was done.
The physical GPU is Quadro T1000 Max-Q, Chromium 153.0.8010.52, with the same
process-local NVIDIA 610.43 libraries for every trial.

The measured window is guest seconds 78–96, rendering enabled, capture/readback,
profiling and detailed counters disabled. `profile.ts` now explicitly supports
`EKA2L1_SHARED_AUDIO=1` and records it in the report. Previously this harness
selected the legacy silent DSP even though live play used the shared driver.
The shared DSP and Cubeb resampling run here with fixed guest-clock callbacks;
the benchmark retains PCM/events for export and does not play through the host
AudioWorklet. These are comparable throughput controls, not a new live audio-
continuity or input-latency test.

| Independent experiment | Baseline host seconds | Candidate host seconds | Baseline / candidate mean |
| --- | --- | --- | --- |
| Deferred flags | 16.4610 / 14.5018 | 17.0851 / 17.2969 | 15.4814 / 17.1910 |
| Incoming-register entry definitions | 14.1169 / 14.6291 | 15.1050 / 15.7940 | 14.3730 / 15.4495 |

Both candidates are slower than both controls in their respective batches.
Neither earns promotion. These are two trials per variant on a shared host,
not estimates of a universal intrinsic regression. The unchanged baseline
means correspond to 1.16x and 1.25x unpaced throughput with the shared DSP;
this is distinct from the earlier paced 1.00x live observation.

Every run executes 3,975,618,624 guest instructions and 676 presentations.
The audio-enabled instruction count differs from older silent benchmarks;
these batches must not be pooled with those older measurements.

## Correctness and limits

Both final candidates pass 134 WASM tests, including **453,936 exact
budget/register/flag/memory comparisons**, and all **672 native/WASM fault
comparisons**, including callback state/order. Both checked 1,600-image browser
runs match the existing native shared-DSP reference, with identical guest
records and **4,919,249 stereo PCM frames** through 102.484363 guest seconds and
16,263,331,210 instructions. Native test targets pass. Seven frontend checks pass for the entry candidate
and the restored default.

The old movement heuristic is **not passing**: it fails identically on the
native reference and both candidates. All 1,600 viewports are unique, but the
maximum guest presentation interval is 408,561 us (its limit is 100,000), and
minimum changed viewport fraction is 0.002483 (its limit is 0.01). Exact pixels,
guest timestamps and the complete heuristic output agree across the reference
and candidates. No thresholds were weakened, and this failure is not described
as a passed gate or a newly introduced performance regression.

## Decision

Both production changes are removed. The research patches, expanded differential
fixtures, synthetic flag probe and explicit shared-audio performance-harness
selection are retained. No new whole-game speedup is claimed. The flag probe
establishes a local benefit after warmup, but the previously captured hot
routines are not flag-heavy and the full-game result does not justify enabling
this mechanism. The conservative register-entry analysis likewise does not
earn its cost in these controls.

The original emitter and state-local cache are restored. The LAN launcher stays
on its previously verified audio archive; these candidates were never deployed.
No new live-play, audio-continuity, native Qt headroom or second-game test is
claimed. The restored build again passes all 134 WASM tests, 672 fault cases, native
targets, seven frontend checks and a fresh checked 1,600-image shared-audio
replay. Its hash differs from the archive (including a changed embedded build
revision), so the fresh replay verifies that rebuilt artifact independently.
The HTTPS-served hash still exactly matches the verified audio archive.
Checks and hashes are recorded in the evidence.
All work remains local on `wasm-port`, with no push.

## Reproduction

Research patches apply to the baseline emitter at `7219855a2`. Build the flag
patch with `EKA2L1_WASM_LAZY_FLAGS=ON`. The entry patch is independent.
Use `test_aot_wasm --emit-flags` through Node to export the base64 `FLAG_MODULE`,
then decode it to a `.wasm` file for each version. Run:

```sh
node src/tests/wasm/flag-kernel.ts baseline.wasm candidate.wasm NEW_REPORT.json
source /home/claude/.scratch/eka-benchmark/validity-gpu.env
EKA2L1_SHARED_AUDIO=1 python3 src/tests/benchmark/serial_build_comparison.py \
  ASSETS BASELINE_BUILD CANDIDATE_BUILD NEW_OUTPUT
EKA2L1_SHARED_AUDIO=1 EKA2L1_BENCHMARK_AOT=5 EKA2L1_AOT_VERIFY=1024 \
  node src/tests/wasm/benchmark.ts ASSETS NEW_REPLAY 1600 \
  src/tests/benchmark/snakes.input 21000000
python3 src/tests/benchmark/compare.py NATIVE_REFERENCE NEW_REPLAY
```

Raw rounds, per-run GPU/build metadata, correctness records, unchanged movement-
heuristic failures and restore checks are in `FLAG_DATAFLOW_EVIDENCE.json`.
