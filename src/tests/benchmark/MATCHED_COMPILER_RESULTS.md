# Matched native/WASM control and a precise memory-miss prototype

This investigation implements the requested same-source comparison and uses it
to select one general compiler experiment. It does not identify a universal
WASM penalty or apportion the entire Qt/browser performance gap.

## Same implementation on both targets

`matched_kernel_reference.h` is a restricted, template-unrolled ARM reference.
It implements the captured 57-instruction math routine and seven-instruction
memory prefix. Guest bytes remain in local fixtures, outside Git. Both targets
compile this same source with SDK Clang 21 at `-O3`. Native uses the normal
x86-64 target; WASM enables 128-bit SIMD, allowing vectorized state copies as on
native. The reference/harness translation unit uses SDK Clang on both targets;
production helper libraries retain their respective project toolchains. The
C++ function-pointer batching loop is identical on both targets.
The browser additionally calls the current emitter and the experiment through
that same loop, using real WASM function imports, without JavaScript per call.

The reference retains per-instruction budgets, permission-specific TLB checks,
alignment/page/endian handling, cached pages, block-transfer spans, code-write
exits, and state publication/reload around production memory helpers. Its
allocation addresses and pointer widths naturally differ across targets. This
is a controlled comparison of the implementation and toolchains, not identical
machine instructions or a claim that this reference is an optimal native JIT.

A native Dynarmic Step oracle supplies every budget boundary across 16 seeded
fixtures. Each final browser batch performs **3,168 exact register/flag/count/
memory comparisons**, plus **528 reference/current-emitter comparisons** with
unaligned, endian, absent/read-only/zero-tag mappings and code-write aliases.
Both reference targets match **672 native fault cases**, including exception
callback state/order and every changed memory byte.

Two complete native/browser batches ran serially, with eight alternating timing
rounds per browser variant and no simultaneous owned build/replay/profiling job.
Timed calls use the full routine budget and the same seed-72 ordinary-memory
fixture; fault behavior and short budgets are checked separately, not timed.
All measured rounds are retained. Medians below combine 16 rounds; host-load
outliers remain in the evidence. Five million calls per cell:

| Kernel | Same C++ native | Same C++ WASM | Current emitter WASM | Prototype WASM |
| --- | ---: | ---: | ---: | ---: |
| Math, 57 instructions | 525.79 ms | 557.06 ms | 588.42 ms | 487.38 ms |
| Memory, 7 instructions | 98.12 ms | 119.87 ms | 114.19 ms | 111.79 ms |

The same-source browser/native elapsed ratios are **1.059x** and **1.222x**.
The existing emitter trails the compact reference by about 5.6% on math, but
beats it on the prefix. The prototype improves math throughput **1.207x**;
prefix gains are small and inconsistent between batches (roughly 0–8%).
These results do not measure whole-game dispatch, code validation, scheduler
work or JIT entry/linking. The native reference deliberately carries our
memory/budget policy, whereas Qt's Dynarmic has different code and amortization.
It would be incorrect to call the 6–22% differences an unavoidable browser floor,
or to infer that our entire compiler is only 6% from optimal.

## Why this prototype

The first C++ reference outlined memory accesses and lost time publishing its
register frame. Keeping accesses and instructions inline, and enabling SIMD
state copies, improved the reference. These exploratory timings are preserved
separately; the table uses the final, identical source and nonzero code guards
on both targets. The first GCC trial also stopped at a missing prefix fixture;
it is excluded from the matched comparison.

Actual TurboFan code was extracted separately from timing. Current math occupies
38,272 bytes; the prototype occupies 25,216, with fewer static stack operands
and cold helper calls. The final C++ reference is 23,296 bytes (native Clang
14,787). For the prefix, current/prototype/reference are 6,208/1,856/4,096 bytes.
Code size and static instruction counts are descriptive, not speed estimates.
Inspection runs overlapped correctness work and are excluded from timing claims.

The production prototype recognizes ordinary pre-indexed loads/stores without
writeback or a PC result, and multi-register spans without writeback or PC.
On a fast-path miss, it returns **before** the instruction, undoing its tentative
compiled instruction count. DynCom then performs the instruction with its real
memory/fault machinery. Successful direct accesses keep all existing permission,
alignment, endian, code-write and budget checks. Entry byte/dependency validation
is unchanged. No guest PC or game name selects the optimization.

The first broader prototype failed 176 callback-state comparisons: DynCom
publishes block-transfer base writeback earlier than the compiled/native path.
Final register/memory outputs alone would have missed this. Writeback and PC
forms therefore keep their original helper path. The restricted prototype passes
all **672** cases; **356** cases actually take the deferred path. A further
**672** direct tests check precise pre-instruction exits, counts, register state
and absence of partial memory effects.

## Whole-game acceptance

Serial unprofiled old/new/new/old trials (one browser, including warmup)
produced the following host seconds for 18 guest seconds:

| Order | Default | Prototype |
| --- | ---: | ---: |
| First pair | 19.6035 | 17.2213 |
| Reverse pair | 29.5235 | 30.2768 |

All four execute **3,975,200,506 guest instructions** and **676 presentations**.
The first pair favors the prototype; the reverse pair does not. Both builds
slow markedly in the latter pair. All observations are retained; no causal
speedup is assigned to the roughly 3.4% difference between pooled means. As
requested, no separate host-load investigation was undertaken. The 1.25x
heavy-scene target is not established.

The subsequent serial real-UI runs give:

| Build | Guest / host seconds | Paced ratio | Maximum sampled lag |
| --- | ---: | ---: | ---: |
| Prototype | 122.429 / 120.494 | 1.0161x | 0.342 s |
| Default control | 112.839 / 120.700 | 0.9349x | 8.470 s |

Both pass manual upload/Start, keyboard and touch delivery, changing gameplay,
narrow layout, blur release and shutdown. Intermediate scene captures show
active gameplay. This pair favors the candidate, but paced catch-up is not an
unpaced headroom measurement, and a single pair does not establish a universal
regression or gain. It does not resolve the contradictory heavy-window pairs.

**Decision:** retain the restricted prototype as an opt-in research path, OFF by
default. The repeatable math-kernel gain establishes a useful emitted-code
improvement; dependable whole-game headroom and deployment acceptance remain
unestablished. Do not reinterpret the same-source 6–22% kernel differences as
WASM's share of the entire Qt gap. Our execution policy and the real dispatcher
still differ from native Qt. The served baseline remains unchanged.

The final candidate passes 133 WASM tests,
all three native test targets, seven frontend checks, and the checked
1,600-image native-interpreter replay through 102,484,363 guest microseconds and
16,261,337,499 instructions. Pixels, guest records and audio records match.
The preceding, less restricted build also passed a complete checked replay.
The sampled interpreter checker disables direct-memory execution on sampled
calls; synthetic fast-path comparisons and full replay provide additional
coverage. No second game has been tested.

The local default was rebuilt with the experiment OFF and again matches all
672 fault cases and passes all 133 WASM tests. The served LAN build remains pinned to the prior verified
default (SHA-256 `e0c7bbd367e32fa6a6cf61983ea2bba2ec080fabe5a000e67c1df234e4118c62`);
its HTTPS response and binary hash were verified after the live trials.
`EKA2L1_WASM_DEFER_MEMORY` is a research CMake option, OFF by default.

## Reproduction

Configure both builds with
`-DEKA_MATCHED_KERNEL_FIXTURES=/absolute/path/to/cpu-kernels-base`, containing the
two captured `.arm` files. Build optional `eka_matched_kernel` and
`eka_matched_fault` targets. `generate_matched_kernels.py` creates template
instantiations only inside the build tree. Build the native comparison object
with `build_matched_clang.py build SDK/upstream/bin/clang++ NEW_OUTPUT`; run its
`matched-clang NATIVE_ORACLE_DIR`. The existing native oracle generator is
`eka_cpu_kernel_native`; `pack_cpu_kernel_fixtures.py` supplies browser fixtures.
Run `node src/tests/wasm/matched-kernel.ts WASM_TARGET_DIRECTORY FIXTURES NEW_OUTPUT`.

`eka_cpu_fault_native --extended` and `eka_matched_fault --extended` generate
672-case logs. Run WASM `eka_cpu_fault_wasm.js --deferred` for the prototype and
compare with `compare_cpu_faults.py ... --cases 672 --require-equal`.
The original 480-case interface is retained.

For the real game, configure `-DEKA2L1_WASM_DEFER_MEMORY=ON`, leaving code versions
and lifecycle OFF. `serial_build_comparison.py ASSETS BASELINE CANDIDATE NEW_OUTPUT`
runs old/new/new/old browsers one at a time, including warmup, over guest seconds
78–96 with rendering on, no capture, no profiling and the physical GPU. Set the
same process-local GPU environment for both builds. `benchmark.ts` and `live.ts`
retain their existing replay/live acceptance roles.

## Real Snakes repeat, 2026-09-28 16:31 UTC

At the user's request, repeated the actual Snakes benchmark, using the exact
same archived baseline and prototype binaries (hashes above/in evidence), not
an extracted kernel. The earlier whole-game timings also used real Snakes.
One browser ran at a time, including warmup; rendering stayed enabled on the
physical NVIDIA GPU, with capture/readback, profiling and diagnostic counters
disabled. No concurrent owned build or benchmark ran. Order: old/new/new/old.

| Build | Host seconds for guest seconds 78–96 | Mean |
| --- | ---: | ---: |
| Baseline | 18.6526 / 17.9644 | 18.3085 |
| Deferred-memory prototype | 17.2244 / 16.0909 | 16.65765 |

Both pairs favor the prototype. The mean throughput improvement is **9.91%**,
and the prototype reaches **1.0806x realtime** in this heavy window. All four
runs execute **3,975,200,506 guest instructions** and **676 presentations**.

This is a positive whole-game repeat, not just the earlier 21% kernel result.
It still has only two trials per build on a shared host, and does not erase the
previous contradictory batch. The 1.25x headroom target remains unmet. The
prototype remains opt-in/OFF by default; the public launcher is unchanged.
No compiler changes were needed, so prior 133-test, 672-fault-case, checked
1,600-image and live-workflow validation applies to these identical binaries;
those gates were not rerun in this repeat. Detailed reports, GPU information,
execution totals and binary hashes are in `MATCHED_SNAKES_REPEAT_EVIDENCE.json`.

## Graduation, 2026-09-28

Following the positive real-Snakes repeat, the user authorized graduation.
`EKA2L1_WASM_DEFER_MEMORY` now defaults to ON; OFF remains an explicit comparison
and rollback option. Earlier OFF/default decisions above describe their original
measurement stage. The graduated binary is the same hash-verified candidate
used by the fault, replay and live checks, not a new untested implementation.
Marginal experiments are reassessed separately in `GRADUATION_RESULTS.md`.
