# Safe validity generations and native/browser cost comparison

Implementation: `ef72fcc28`; opt-in gating and profile tooling: `756061e7f`.
The version implementation passes correctness, but **does not establish a net
performance gain**. It is retained as an opt-in experiment;
`EKA2L1_WASM_CODE_VERSIONS` defaults **OFF**. The served default retains exact code
byte checks and does not emit guest version-write barriers.

## What was implemented

See [CODE_VALIDITY.md](CODE_VALIDITY.md). Versions follow physical backing pages,
so aliases share invalidation. Mapping/address-space/mode/backing/extent checks
remain. Guest stores, interpreter and direct compiled paths, block transfers and
inlined code dependencies participate. Mutable host-pointer exposure permanently
forces exact byte checking for the allocation; this covers retained pointers
without assuming their lifetime. Unknown backing, reuse and counter overflow
also fall back. Existing within-region code-write exits remain mandatory.

Read-only IPC strings now use bounded copying, avoiding unnecessary mutable
pointer exposure. No game addresses or names select this optimization. The
tracking table costs 8 MiB plus cached stamps; it is not allocated by the default
or native build.

The heavy diagnostic window records 154,174,588 version hits and 63,690 exact
checks (99.959% hits among these checks). Avoiding scans is not free: writes pay
barrier costs and cache entries check page stamps. We have not isolated the
individual barrier cost from all other generated-code work.

## Controlled throughput result

Same 78–96 guest-second Snakes replay, rendering on, capture off, no pacing,
physical Quadro T1000 Max-Q. All browser timing controls use Chromium
153.0.8010.52. Runs were warmed before the serial forward/reverse measurement
sequence; no profiling or fine-grained counters during these timing windows.

| Variant | Host seconds | Mean |
|---|---:|---:|
| Previous exact-byte build | 20.7938, 23.9035 | 22.3487 |
| Safe versions | 22.3131, 29.4221 | 25.8676 |

The candidate mean is 15.7% higher elapsed time, but the ranges overlap and host
variation is substantial. Separate diagnostic sampling runs move in the opposite
direction (24.7080s before, 20.4461s tracked). These do **not** support a reliable
speedup or a precise causal regression estimate. In particular, the earlier
unsafe scan bypass was an upper-bound experiment that paid no real barrier cost.
We keep the safe implementation available, but do not deploy it as a performance
improvement.

Both variants execute 3,975,200,506 guest instructions and 676 presentations.
Fresh uninstrumented Qt JIT controls take 4.1278/4.13126s. Native executes
3,981,611,134 instructions, 0.161% more, and the same 676 presentations. Native JIT
budget exits differ: this is a close workload comparison, not the exact
interpreter replay correctness gate.

## Common phase instrumentation

Separate diagnostic runs instrument the same scopes in native and WASM. WASM
below is the tracked candidate; native values average two Qt Dynarmic runs.

| Scope | WASM seconds | Native seconds | Approximate ratio |
|---|---:|---:|---:|
| Guest CPU run | 20.4165 | 3.9630 | 5.15x |
| Graphics wait | 1.0289 | 0.1731 | 5.94x |
| Scheduler | 0.6817 | 0.2468 | 2.76x |
| Timer processing | 0.6081 | 0.0463 | 13.1x |
| Audio scheduling | 0.0184 | 0.0053 | 3.50x |

These scopes overlap: CPU execution can wait for graphics. **Do not sum them.**
Instrumentation also has cost, especially hundreds of thousands of tiny timer
and scheduler scopes; those ratios are not clean estimates of uninstrumented
service costs. The practical result is clear at the coarse level: the CPU path
is by far the largest absolute gap. Graphics waits are secondary. Audio is tiny.
The Qt detailed runs take 4.3435/4.3914s versus ~4.13s without instrumentation;
the WASM detailed run takes 24.4541s.

## Sampling interpretation

Browser CDP profiles separately sample the guest worker. Native samples record
only the guest thread's current instruction pointer using a Linux per-thread
signal timer; Dynarmic's existing perf maps associate generated PCs with guest
block entries. Native has no inferred call stacks. Self categories are disjoint;
WASM inclusive generated-code time includes helper callees and must not be added
to self categories.

A 1ms native run was severely perturbed (15.62s versus ~4.13s controls), so it is
retained as a failed diagnostic rather than used as a representative cost model.
The lower-rate checks and sample attribution are recorded in the evidence JSON.
No individual guest-entry ratio is treated as an equivalent function comparison:
WASM region entries include inlined callees, while native blocks are finer.

## Where WASM is disproportionately expensive

Lower-rate native sampling (5ms) takes **4.2522 / 5.5119s**, with 850 / 1,102
samples and ~100% nominal timer coverage. Surrounding unprofiled controls take
4.2325 / 4.1784s before and 5.3458 / 5.5193s after. The second sampled run slowed
alongside the host controls; the severe 1ms outlier does not recur. These coarse
samples support broad categories, not precise attribution to individual opcodes.

| Disjoint self-sample category | WASM, default byte checks | WASM, tracked experiment | Native JIT, two 5ms runs |
|---|---:|---:|---:|
| Generated guest code | 14.391s | 12.388s | 3.417–4.421s |
| Compiled lookup / validity | 5.496s | 3.874s | Not separately comparable |
| Outer CPU loop, including compiled dispatch | 2.916s | 2.372s | Not separately comparable |
| Other runtime | 1.117s | 1.152s | 0.490–0.605s |
| Unresolved generated dispatch/stubs | Included in the named browser categories | Same | 0.190–0.225s |
| Waiting / profiler boundary (browser), synchronization (native) | 1.050s | 0.920s | 0.155–0.260s |

The rows are categories within each profiler, not one-to-one backend functions.
Native has no equivalent validated-code-cache scan path. Its unresolved generated
addresses cannot safely be classified more finely. Browser sampled spans include
attach/stop boundaries and total 24.971s / 20.707s, slightly longer than their
24.708s / 20.446s measurement windows. **The two browser columns are separate
noisy diagnostic runs, not evidence of a causal speedup.**

The strongest findings are:

1. **Generated execution itself is much more expensive in WASM.** Even excluding
   surrounding lookup/dispatch, it takes roughly 12–14 sampled seconds versus
   3.4–4.4 native seconds. These generated functions include our memory guards,
   guest-state traffic and budget/exit machinery, not just useful guest arithmetic.
   Native spends ~80% of its self samples in named guest blocks; WASM has a much
   larger surrounding runtime burden as well.
2. **Lookup plus outer dispatch is the next substantial excess.** Together they
   take 8.412s in the default browser profile and 6.246s in the tracked diagnostic.
   `InterpreterMainLoop` includes compiled dispatch: its samples do not mean we
   returned to interpreting that proportion of guest instructions. Versions
   address only part of this cost and add work elsewhere.
3. **Graphics synchronization and services are secondary.** Common scope timers
   show ~1.03s graphics wait versus ~0.17s native. Timers/scheduler are relatively
   slower too, but their absolute size and instrumentation overhead make them a
   weaker first target than generated execution and dispatch. Audio scheduling
   is negligible for this comparison.

The next bounded compiler experiment should compare the generated native code
for the math/memory regions around `0x7006370c` and `0x70013edc`, matching exact
budgets and fault/code-write semantics, then remove demonstrated redundant state
or guard work through a general IR/data-flow optimization. Both are prominent
browser regions; entry-page sampling is included in the evidence, but inlining
prevents claiming an exact native/browser routine multiplier. A second target is
reducing generic lookup and dispatch by connecting hot successors while keeping
state local and retaining safe exits. More byte-scan elimination alone is not
supported as the primary next step by this experiment.

## Validation and reproduction

- Tracked build: two complete 1,600-image runs match native-interpreter pixels,
  guest timestamps/instruction counts and PCM/events exactly. One uses a
  deterministic one-in-1,024 compiled-block interpreter check.
- Default restored build: another 1,600-image replay matches the same reference.
- Endpoint: 102.484363 guest seconds, 16,261,337,499 guest instructions.
- Full WASM suites: 134 tests with versions; 132 with the option off (two version
  tests explicitly skipped). All three native targets pass: 320 package cases,
  28 CPU cases, two network cases. Seven frontend checks pass.
- No additional game or audio-quality test. Sound remains off.

Enable the experiment with `cmake -S . -B build-wasm -DEKA2L1_WASM_CODE_VERSIONS=ON`, then rebuild the frontend and WASM test target.
Switch it back OFF and rebuild to restore the default. Use the established
Emscripten toolchain configuration in that build directory.

`run_qt_profile.py --sample` enables native PC sampling and Dynarmic perf maps;
`EKA2L1_NATIVE_SAMPLE_PERIOD_US=5000` selects the lower 5ms frequency.
`--detail` collects common phase timers separately. Plain runs select neither.
Use `summarize_native_profile.py` on `native-pcs.tsv`, then
`compare_native_wasm_profiles.py` with a summarized browser profile.

Raw files are under `/home/claude/.scratch/eka-benchmark/validity-*`; hashes,
measurements, environment, artifacts and limitations are in
[VALIDITY_AND_COST_EVIDENCE.json](VALIDITY_AND_COST_EVIDENCE.json).

The host changed during this work: Chromium 150 became 153, and installed NVIDIA
libraries no longer matched the loaded kernel driver. Fresh comparisons use the
same Chromium 153 and process-local matching NVIDIA 610.43.03 libraries extracted
from the official Arch archive. System libraries were not downgraded. Older
Chrome 150 timing batches are not causal controls for this experiment.
