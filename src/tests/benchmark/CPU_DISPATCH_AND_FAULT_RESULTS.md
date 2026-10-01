# Actual dispatcher costs and native/WASM fault comparison

Baseline production source: `350b15d74`; research tools: `de9ec0f06`.
No performance optimization or fault fix is deployed by this investigation.

## Findings

The former combined lookup/validation/dispatch bucket contains several different
costs: RAM cache and mapping-generation guards, exact byte validation of the
region and its inlined dependencies, lookup-wrapper state publication, ROM
lookup, and the runner's prepare/call/return/budget bookkeeping. The experiment
below separates these on the **real game execution path**, rather than timing
an isolated direct-import call.

The new fault probe **disproves general native/WASM fault equivalence**. Ordinary
memory timing remains valid within its stated fixture, but it cannot stand in
for equivalence across denied permissions and endian modes. These differences
must be accounted for before claiming a semantics-equivalent optimized memory
path.

## Dispatch measurement method

Same 78–96 guest-second Snakes replay, NVIDIA GPU, rendering enabled, capture
and detailed phase timers disabled. Detailed timers were disabled because
millions of clock calls measurably perturb this experiment. Guest virtual time
and instruction budgets are unchanged.

The diagnostic patch gives cache/validation and runner stages separate function
boundaries. LLVM `noinline` alone was insufficient: Binaryen and then V8 inlined
some of them again. The successful diagnostic uses a process-local Binaryen
wrapper to mark AOT functions non-inline before optimization, plus Chrome's
`--no-wasm-inlining`. Neither is installed into the normal served build. This
**perturbs execution**; its split cannot be treated as exact production timing
or as additive promised savings. Unprofiled diagnostic and production controls
quantify the perturbation, and a production profile supplies the outer context.

`dispatch_breakdown.py` assigns each guest-worker sample to one category using
its stack. Generated-region samples and their callees are separated first;
validation callees are then assigned to the primary or dependency scan that
called them. Remaining RAM-cache samples exclude those scans; remaining runner
samples exclude generated execution and lookup. These are disjoint buckets,
not inclusive scopes to be added twice. Sub-millisecond or absent categories
are not evidence of zero cost.

Native sampling uses the actual Qt Dynarmic guest thread at 5ms intervals.
A research-only perf-map sidecar preserves permanent dispatcher/return-cache/
memory-fallback names which `PerfMapClear()` otherwise erases. It adds symbol
records when code is generated, not clocks around each block. Native samples
are PCs rather than stacks; memory helpers are reported separately. Direct
linked jumps and terminal logic inside guest blocks remain charged to those
blocks, so the named native dispatcher bucket is not the complete cost of all
control transfers.

## Measured dispatch breakdown

Unprofiled serial controls, host seconds for the same 18 guest seconds:

| Configuration | Trial 1 | Trial 2 |
|---|---:|---:|
| Production browser | 17.5476 | 17.8158 |
| Diagnostic browser, preserved stage boundaries | 19.0315 | 19.3280 |
| Native Qt Dynarmic | 4.8830 | 4.1647 |

Production browser is **3.91× slower by these means**. The diagnostic changes
alone increase browser elapsed time **8.5%**. Native executes 3,981,611,134
instructions versus browser 3,975,200,506, a 0.161% difference; both produce 676
presentations. This is closely matched guest work, not exact backend parity.

The following are **sample-weighted seconds in two 5ms diagnostic profiles**,
not unprofiled stage stopwatches:

| Real browser path, disjoint categories | Run 1 | Run 2 |
|---|---:|---:|
| Generated regions and their callees | 13.797 | 13.751 |
| RAM cache lookup and mapping-generation guards | 2.023 | 2.337 |
| Primary region byte validation | 1.598 | 1.765 |
| Inlined-dependency byte validation | 0.501 | 0.506 |
| Remaining validation bookkeeping | 0.369 | 0.481 |
| Runner prepare/call/return/budget accounting | 2.524 | 2.449 |
| Lookup wrapper and guard/state publication | 1.189 | 1.255 |
| ROM registry lookup | 0.172 | 0.222 |
| Outer CPU dispatcher self | 0.106 | 0.081 |
| Other runtime | 0.708 | 0.698 |
| Wait/profiler boundary | 0.984 | 1.174 |

Thus byte validation plus its bookkeeping is **2.47–2.75 sampled seconds**;
RAM lookup/guards another **2.02–2.34s**; runner bookkeeping **2.45–2.52s**;
and lookup-wrapper publication **1.19–1.25s**. These identify real work outside
the generated functions. The guard category does **not** mean the MMU resolves
every mapping again: actual mapping-refresh calls received no samples in these
lower-rate runs and only one sample in one earlier run. Cached mapping guards
and lookup bookkeeping still execute on the common path.

An independent detailed-counter run over identical guest work counts
179,039,935 compiled region invocations, 154,207,391 RAM invocations and
1,013,214 runner calls: about 22.2 compiled guest instructions per region and
176.7 regions per runner call. Increasing the outer chain limit alone would
leave the per-region work above.

**Observer effects remain substantial.** The two 5ms split profiles take
23.7931/24.4415 wall seconds, versus 19.03/19.33 without sampling. Earlier 1ms
profiles take 22.4572/25.8375s and give the same broad attribution (generated
12.73–14.99s, validation 2.41–2.73s, RAM lookup/guards 2.04–2.24s, runner
2.63–2.83s). Lower sampling frequency did not eliminate perturbation. No exact
production stage ratio or recoverable speedup is inferred from these seconds.

The unchanged production build at 5ms takes 23.6866s while sampled. Its coarser
buckets are generated code/callees 13.74s, RAM cache 2.41s, validation 1.90s,
lookup wrapper 1.39s and outer CPU self 2.64s. In this build the runner is
inlined into that outer function; this is **not 2.64s of interpreter fallback**.
The fine scan/runner boundaries are absent, as recorded explicitly by the
parser. Exploratory runs with detailed phase clocks enabled are archived but
excluded; one spent 4.18 sampled seconds in clock retrieval alone.

Native 5ms PC samples separate the following real code ranges:

| Native Qt path | Run 1 | Run 2 |
|---|---:|---:|
| Generated guest blocks, including their terminal/link logic | 3.210 | 3.322 |
| Return prediction and successor lookup stubs | 0.160 | 0.195 |
| Dispatcher entry/exit stubs | 0.010 | 0.005 |
| Memory helpers | 0.040 | 0.025 |
| Context save/load | 0.090 | 0.075 |
| Synchronization | 0.195 | 0.160 |
| Other native runtime | 0.440 | 0.340 |
| Unresolved generated address | 0.005 | 0 |

Native sampled wall times are 4.1505/4.1229s and guest-thread CPU times
3.9771/3.9406s. Return prediction accounts for 0.140/0.160s of the successor
stub bucket; fast dispatch is 0.020/0.035s. These few samples do not establish
sub-millisecond precision. Native direct links remain inside generated blocks,
so comparing its 0.17–0.20s named stubs to every browser dispatch stage would
understate native control-flow work. Native has no corresponding repeated
exact-byte validation stage.

The evidence supports **two distinct CPU gaps**: generated code plus its guards,
and repeated work between generated regions. It does not support attributing
the entire gap to cross-module call mechanics or to remaining interpreter work.


## Fault/state verification

The standalone probe links the actual native `dynarmic_core` or WASM
`dyncom_core` plus the production translator, module loader, raw memory imports
and compiled runner. It does not replace memory helpers with permissive fake
imports. It asserts that both test instructions ran compiled in every WASM case.

The 480 synthetic cases cover LDR/STR, byte and halfword accesses, LDM/STM,
aligned/unaligned/page-boundary addresses, little/big endian, an absent data TLB
entry or a read-only entry, and four exception-handler outcomes: repair and
retry; reject without stopping; reject and request stop; or request a retry
which still fails. Extra LDM/STM cases allow the first word before faulting on a
later word. A preceding MOVS establishes a changed register/flags state.

Native `Step()` is the precise single-instruction reference; WASM executes the
same one-instruction budgets through its normal dispatch path. Counters are
normalized because native Step accumulates its count while DynCom resets it.
We compare all 16 registers, CPSR, count, **every modified memory byte** relative
to the identical full initial memory, callback order/access sizes/results,
exception kind/address, and registers/CPSR visible inside the handler. The
memory hash is supplemental; it is not substituted for exact changed-byte
comparison.

| Result | Cases |
|---|---:|
| All final state, memory and callback fields agree | **309 / 480** |
| Final state and memory agree, irrespective of callback differences | **348 / 480** |
| Little-endian, no TLB entry: all fields agree | **120 / 120** |
| Read-only TLB store path differences | **120** |
| Big-endian TLB read differences | **48** |
| Big-endian halfword store ABI differences without TLB | **3** |

The three difference groups are disjoint. Examples and every mismatching
synthetic case are committed in `CPU_FAULT_CASES.json`.

1. **Denied writes bypass the exception path.** Generated code checks the write
   tag, but its helper fallback reaches `ARMul_State::WriteMemory*`, which uses
   the generic `r12l1::tlb::lookup`. That lookup accepts a matching read, write,
   or execute tag. It can therefore write through a read-only entry. Native
   Dynarmic calls the failing write callback and exception handler. With a
   repairing handler, final bytes may agree despite a missing exception; with
   an unrepaired access they can differ. Both callback and final-state checks
   are necessary.
2. **TLB reads omit endian conversion in the shared helper.** `ReadMemory16/32`
   returns a raw host value on its generic TLB hit. The generated big-endian
   fallback reaches that path; native swaps the value. Example: the same four
   bytes at `0x8000` give native `0x0b30557a` versus WASM `0x7a55300b`.
3. **The halfword import has a narrow C++ ABI parameter.** The generated module
   passes a full i32 register value to `raw_write16(..., uint16_t)`. C++ expects
   its caller to have narrowed that value. With `r0=0x87654321`, big-endian STRH
   writes bytes `43 65` in WASM versus native `43 21`. This differs from the
   interpreter control and identifies an additional generated-call boundary
   problem, rather than treating every discrepancy as a TLB issue.

A separate WASM interpreter control reproduces the shared-helper permission and
TLB-endian problem; it also differs in some callback-visible partial-transfer
state. The compiled path improves on that interpreter state exposure in the
120 little-endian absent-TLB cases, which all agree with native. It is therefore
not sufficient to use DynCom alone as an oracle for all fault semantics.

These checks validate the emulator's **callback contract**, not a full hardware
ARM data-abort exception implementation. An exception callback may return false
without stopping; current cores can then commit a zero read or continue a block
transfer. Page-boundary cases use contiguous fixture backing; they do not establish
correctness for all discontiguous MMU mappings. Native linked Run exits and arbitrary faults across long linked
paths are not proven identical by these Step fixtures. The observed differences
already falsify an unrestricted equivalence claim; they are not hypothetical
limitations.

## Implication

Prioritize the generated memory path and the real runner/cache round trips as
separate targets. The matched ordinary-memory microbenchmarks identify guard
and budget costs; this real-path profile identifies additional work outside the
generated region. Putting a kernel in the caller's module does not measure or
remove that surrounding work.

Correct the shared memory-helper permission/endian handling and the narrow
import ABI before using these paths as a semantics-equivalent performance
control. Retain exact budgets and byte checks; the unsafe guard-removal timings
remain diagnostic ceilings. No new speedup, Qt parity, or general fault parity
is claimed here.

## Reproduction

- Build `eka_cpu_fault_native` in the native tree and `eka_cpu_fault_wasm` in the
  Emscripten tree. Save their stdout, then run `compare_cpu_faults.py NATIVE_LOG
  WASM_LOG OUTPUT_JSON --require-equal`. This parity gate currently fails on the
  recorded 171 differences; omitting the flag produces an analysis report without
  a failure exit. Both inputs must contain exactly 480 distinct cases.
  `eka_cpu_fault_wasm.js --interpreter` runs the separate interpreter control.
  Leave `EKA2L1_AOT_VERIFY` unset for this probe; its explicit native comparison
  is the oracle, and enabling the replay checker would add observer reads.
- Apply `dispatch_symbols_experiment.patch` only to a diagnostic build. Create
  an isolated tool wrapper with `dispatch_binaryen_wrapper.py SDK_UPSTREAM
  NEW_DIRECTORY`, then build with `EM_BINARYEN_ROOT=NEW_DIRECTORY`. Archive the
  resulting launcher files outside the served directory and restore the patch.
- Run `profile.ts` with `EKA2L1_WASM_BUILD_DIR` pointing at that archive,
  `EKA2L1_PROFILE_DETAIL=0`, `EKA2L1_PROFILE_INTERVAL_US=5000`,
  `EKA2L1_BENCHMARK_AOT=5`,
  `EKA2L1_PROFILE_START_US=78000000`, `EKA2L1_GPU=hardware`, and
  `EKA2L1_V8_FLAGS=--no-wasm-inlining`; arguments end in `2 1 96000000`.
  `dispatch_breakdown.py OUTPUT_DIRECTORY` produces the disjoint stage totals.
- Apply `native_stub_symbols_experiment.patch` inside the Dynarmic submodule
  only for the diagnostic build. Run `run_qt_profile.py --sample` with
  `EKA2L1_NATIVE_STUB_SIDECAR=1` and
  `EKA2L1_NATIVE_SAMPLE_PERIOD_US=5000`. `summarize_native_profile.py` consumes
  both the normal perf map and `native-stubs.map`. Restore the submodule after
  archiving the diagnostic binary.
- Keep sampled runs separate from timing controls. Use the same process-local
  NVIDIA libraries described in `VALIDITY_AND_COST_RESULTS.md` on this host.

## Validation and delivery state

The diagnostic build, with stage-preserving compilation and browser inlining
disabled, passes the **checked 1,600-image native reference comparison** through
102.484363 guest seconds / 16,261,337,499 guest instructions. Every pixel, guest
record, PCM sample and audio event matches; every 1,024th compiled block is also
checked against the interpreter. Visible canvas and shutdown checks pass.
This is the recorded Snakes route, not fault-path coverage.

All **three native test targets and 132 existing WASM tests pass** after restoring
production CPU sources. The new, separate `--require-equal` fault parity gate
**fails** with 171 mismatches. Both full native and WASM fault-probe outputs repeat
byte-for-byte after rebuilding against restored production sources.

The served WASM SHA-256 remains
`950d7a5395795f24ab87472a88ff31a145774ee598a1c8d2f6ecc7622bee77a9`.
No no-inline flag, unchecked memory path, or experimental symbol patch is
installed into normal gameplay. The native submodule source is restored too;
diagnostic binaries, patches and raw measurements are archived separately.
Research remains locally committed on `wasm-port`; nothing is pushed.


## Follow-up

The three fault mechanisms are fixed and the 480-case gate passes in
[Memory semantics and connected-path experiments](MEMORY_AND_CONNECTED_RESULTS.md).
The failing totals above describe the historical diagnostic build.
