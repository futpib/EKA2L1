# Native Sky Force attribution to guest ARM and C++

Native CPU sampling now resolves both generated ARM/Thumb code and the C++
emulator. Two fresh Sky Force combat captures agree: roughly **55–56%** of the
guest worker's samples lie in identified C++ WASM code and **40%** in generated
guest code. The remaining 4–5% are outside recorded JIT code or code versions
without a selected live snapshot. These are sampled self costs, including
inlined work, not exclusive source-line costs or speedup estimates.

The largest measured opportunity is the compiled-region dispatch cycle. This
is more specific than a large `execute_chain` bucket: the native call sequence,
ROM successor lookup, and return bookkeeping are separately visible in the
actual executed machine code. No runtime optimization was adopted in this task.

## Repeatable native hotspots

Percentages use each capture's entire sampled guest worker: 26,576 and 20,909
samples respectively. Both replay 42–60 guest seconds, with the same
6,439,908,640 guest instructions and 576 presentations. Chrome is
153.0.8010.52 / V8 15.3.76.13; the baseline source is `194bad0a0` plus the profiling
metadata changes. Both metadata snapshots recover all 160 selected code versions
and report no scan errors or lost samples.

| # | Native code containing the samples | First capture | Confirmation | Interpretation |
|---:|---|---:|---:|---|
| 1 | C++ `execute_chain` | 31.21% | 30.44% | Runner, inlined lookup, V8 call setup and boundary work |
| 2 | ARM region `0x8019d818`, EUser.dll+0x29b0 | 10.46% | 10.75% | Active-object scheduler loop, not heap allocation |
| 3 | C++ `InterpreterMainLoop` | 9.70% | 9.70% | Outer dispatch plus interpreter fallback; this is not all interpreted guest instructions |
| 4 | C++ `lib_manager::call_svc` | 4.66% | 4.78% | Syscall dispatch and inlined work; called handlers have their own self samples |

The EUser identity is checked against the exact stock ROM
(`89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490`).
The active-object interpretation is established by the earlier
[guest disassembly](CHROME_GAMEPLAY_ANALYSIS.md). It is an internal address,
not a guessed nearest-export symbol.

## What the executed native code shows

Within `execute_chain`, the following manually inspected native ranges have
almost identical sample shares across launches. Their boundaries and complete
instruction excerpts are preserved in the [evidence JSON](NATIVE_ATTRIBUTION_RESULTS.json).
These are native-range classifications, not interpolation of sparse source lines.

| # | Range within the captured code version | First | Confirmation | Work visible in the instructions |
|---:|---|---:|---:|---|
| 1 | `0x1314–0x1378` | 11.01% | 10.94% | Indirect-call table access, bounds/type checks, target/instance loads, stack spills, signature check and call |
| 2 | `0x1668–0x16ce` | 7.08% | 6.49% | ROM range test, page pointer lookup, entry load and missing-function checks |
| 3 | `0x1379–0x142f` | 6.28% | 6.52% | Return reloads, count contract, instruction/block totals, stop/IRQ checks and guest-PC alignment |

The hottest individual address is `execute_chain+0x1372`, immediately following
a V8 code-pointer signature comparison and preceding the indirect call. This is
a **WASM-to-WASM region handoff**, not evidence of a JavaScript round trip.
CPU timer samples can skid or land behind a stalled operation: the branch's
sample count does not establish branch misprediction or make its check removable.
The V8 checks belong to the engine's call machinery. A compiler/runtime design
that executes fewer handoffs could amortize the sequence; deleting a C++ flag
check would not remove that machinery.

The ROM lookup has a dependent page-pointer load followed by a function-entry
load. Samples cluster at their consuming tests (`+0x16aa`, `+0x16c2`). This is
stronger localization than attributing the whole runner to lookup, but does not
prove a cache miss or predict the gain from flattening/caching it. Prior broad
ROM grouping lost performance; its [historical results](DYNAMIC_ROM_COHORT_TIMING_RESULTS.md)
remain relevant negative evidence.

In EUser's scheduler, native loads at `+0x365`, `+0x368`, and `+0x36d` all map to
**guest PC `0x8019d824`**, its conditional three-word load. Their WASM offsets
are 709, 720 and 731. One checked 12-byte span feeds those three loads: shared
translation is already working here. Repeating a translation check for every
word is not the remaining problem in this path.

The surrounding native code still computes N/C/V and spills values for the
comparison before that conditional load. On successful fallthrough, later
flag-writing instructions replace some of those values. This is actual residual
emulation work V8 does not eliminate, not merely extra WASM syntax. However,
[the previous deferred-flags experiment](DEFERRED_COMPARE_MEMORY_RESULTS.md)
already removed such work and failed to establish a gameplay gain: Sky Force
averaged -1.12% CPU throughput despite fewer retired instructions. This capture
supports the presence of the work; it does not overturn that experiment or
justify promoting it without a different, measured implementation.

The actionable next investigation is therefore **how to reduce the frequency or
cost of this measured region-dispatch cycle**, while preserving short budgets,
interrupts, syscalls and mapping validity. A proposal should target a specific
native sequence and then run a controlled gameplay comparison. No entire bucket
above should be advertised as recoverable speedup.

## What is and is not mapped precisely

The C++ build carries optimized line tables and a matching standard source map.
Generated modules carry guest PC/lowering metadata, including relocation through
state caching, pruning, inlining and outlining. The offline analyzer also exports
standard guest source maps with readable PC pseudo-sources. The capture records
actual code versions before Debugger attachment, with native byte hashes,
Liftoff/TurboFan identity, source tables and inline-function metadata.

Concrete C++ examples in the confirmation include `execute_chain+0x12ea` mapping
to `aot_runtime.cpp:319` (the chain limit condition), and
`InterpreterMainLoop+0x1267` mapping to `arm_dyncom_interpreter.cpp:2691` (CPSR
inspection). Inlined code uses its actual inline-function index rather than
looking its WASM offset up in the containing function.

| # | Source coverage | First capture | Confirmation |
|---:|---|---:|---:|
| 1 | Exact guest source anchors | 2,749 samples | 2,260 samples |
| 2 | Exact C++ source anchors | 2,319 samples | 1,863 samples |
| 3 | Combined exact source coverage | 19.07% | 19.72% |
| 4 | Native instructions without a source position | 20,397 | 15,843 |
| 5 | Outside recorded JIT code / no selected snapshot | 1,111 | 943 |

V8's optimized source tables are sparse, predominantly calls and trapping memory
operations. Arbitrary arithmetic between them remains unlabelled. This coverage
is biased toward those operations; the mapped subset cannot price all memory,
flag or state-transfer work. Function/native-instruction attribution is broader
than exact ARM-PC/C++-line attribution. There are no inclusive native call stacks.

The adapter is deliberately restricted to V8 15.3.76.13 on Linux x86-64. The first
capture exposed an initial decoder error: V8 initializes its delta-coded native
position at -1, not zero. That was corrected, retaining the original metadata
and correction manifest. The independent confirmation preserves the raw tables
and uses the corrected decoder directly. A real browser fixture now verifies
that native trapping loads resolve to the exact WASM load opcode. Unsupported
versions and code movement fail rather than producing guessed attribution.

The sampler observes renderer threads present at measurement start. Two new
non-guest threads appeared during the confirmation and are recorded as outside
that sampling set. The identified guest worker was sampled throughout. Software
CPU-clock sampling excludes sleeping time and requires no guest counters.
Neither capture is a controlled throughput comparison: the first overlapped
compiler correctness tests and both include profiling overhead. Their different
elapsed times are not an optimization result.

## Verification and artifacts

- Optimized browser build succeeds with both source-map options enabled.
- Full AOT suite: **178 passed, 0 failed**; the pre-existing crash-repro XFAIL and
  diagnostics-disabled skips remain visible in the log.
- Exact Sky Force replay: **60/60 distinct images**, guest timing/instruction
  progress, PCM hash and audio event hash match the reference.
- Source relocation/cache-pruning fixture passes; its source-mapped and unmapped
  modules have identical non-custom WASM sections.
- Browser probe passes through natural tiering and validates live trap-to-WASM
  opcode attribution. Native parser/source-map tests and six existing CPU/profile
  harness tests pass.
- Both gameplay captures complete, render combat, recover 160/160 selected code
  versions, and report no sampling losses or metadata scan errors.

Raw local captures are under `/home/claude/.scratch/eka-native-attribution/sky-2`
and `sky-confirm`; exact replay is `replay-sky`. The tracked evidence JSON contains
hashes, native hot offsets, ARM/C++ examples and inspected native ranges.
[Reproduction and tool limitations](CHROME_PROFILING.md#native-samples-with-arm-and-c-source-attribution)
cover the build flags and capture/analyzer commands. Normal source-map build
options remain OFF; the production LAN artifact was not replaced.
