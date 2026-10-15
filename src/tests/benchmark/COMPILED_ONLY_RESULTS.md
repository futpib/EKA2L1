# Compiled-only browser CPU

Build 106 was adopted on the public `.lan` service on 2026-10-15, from commit
`cfd78248b`. Native execution and the offline WASM interpreter remain
correctness references; normal browser execution never allocates interpreter
decoded storage. The release caveats below remain open.

## Implementation

The browser dispatches compiled regions immediately. Missing regions compile on
demand; deferred memory accesses recover through a separately cached compiled
instruction. Unsupported translations fail explicitly. There is no interpreter
fallback in the browser CPU loop. The native interpreter remains available.

Required translations include word exclusive accesses through the existing
monitor, BLX immediate, signed halfword multiply/accumulate, doubleword transfers,
STM base-in-list and Thumb high-register PC operations. Callback stops, syscall
continuations, flags and interworking retain their architectural boundaries.

The browser prepares loaded image entry paths and their static successors. It
no longer compiles every EUser/FntStore export or treats every RAM instruction
word as a possible function pointer. Exact matching code can share an installed
function across processes only after checking PC, instruction mode, compiler
policy, executable bytes and folded dependencies in each process's own mapping.

Demand functions consolidate into ordinary modules by image/process lifetime,
keeping their callable table slots. Retired versions cannot replace reused
slots. In the current experiment, immutable ROM entries remain shared across
process exits; process retirement releases RAM translations. The CPU worker returns to its
browser event loop between bounded batches, allowing ordinary garbage collection.
No forced-GC flags or calls are part of production or adoption measurements.

Generated source metadata remains lossless. Version 2 uses offset and signed PC
deltas; version 3 compresses that payload with zlib. The attribution reader also
accepts historical version 1.

The watchdog still controls host yielding. A compilation-only yield that never
dispatched a generated guest body must not advance the unpaced virtual CPU
clock. One dispatch-status bit per CPU run distinguishes that case; it neither
counts instructions nor changes the paced wall-clock model.

## Verification and measurement

The correctness suite compares generated instructions with the independent
interpreter, including short-step, callbacks, syscall and interworking cases.
Dedicated compiled-only tests cover exclusive reservations, precise memory
recovery, stepping, compilation-only yields, cache sharing and module retirement.
A production binary name-section inspection must show no interpreter loop,
decoder, decoded-buffer allocator or reference-step function.

Gameplay checks use `src/tests/wasm/arena-growth-benchmark.mjs` against frozen
builds on private HTTPS ports and the unchanged public control. Chromium uses
hardware ANGLE/Vulkan, a fresh browser profile and a temporary guest disk for
each run. Only Hooked On uses the same prepared installation on both sides.
Guest sound stays enabled; the browser output is muted. Asphalt 3 explicitly
selects sound YES. The build-50 catalogue did not overlap builds, profilers or
correctness tests. Later build-106 comparisons overlapped unrelated Cheerp
build/browser work, so their FPS differences are screening evidence only.

RAM is total browser-process PSS, including renderer, GPU and utilities, rather
than WASM allocation capacity. A 1 Hz sample records the observed startup and
runtime peak; brief peaks between samples can be missed. Reports also retain
RSS, process CPU time, host frequencies, compiler time and start/end screenshots.
FPS is a presentation rate in unpaced execution, not a claim about paced game
speed. N-Gage receives a bounded client navigation check and no FPS claim.

Every accepted game sample must show actual gameplay at both endpoints. Earlier
Asphalt 2 sound-prompt and Super Miners menu samples are excluded. Their high
presentation rates are not game speedups. Sky Force checks the complete HUD,
not just a bright logo. Asphalt 3's N-Gage startup rejection was traced to
[inconsistent preservation-package instructions](ASPHALT3_LAUNCH_INTEGRITY.md).
Failed and successful historical launches remain separate evidence; the exact
known-image repair is validated independently of compiled-only adoption.

## Rejected approaches

| # | Approach | Reason for rejection |
| --- | --- | --- |
| 1 | Remove the decoded arena alone | Lower allocated bytes did not establish lower resident browser RAM; extra compiled modules could cost more. |
| 2 | Compact helper-heavy cold translations and short regions | Excess dispatch and modules increased RAM or substantially reduced gameplay throughput. |
| 3 | Export one shared dispatcher for many guest functions | Browser native-code/partition memory increased. Ordinary distinct function exports are retained. |
| 4 | Pool executable-page snapshots | The small saving did not justify the added cache machinery. |
| 5 | Force V8 garbage collection | Useful only to diagnose reclaimable module storage; excluded from performance and adoption results. |

Raw local evidence is under
`/home/claude/.scratch/eka-compiled-only-20261014/`.

## Complete catalogue pass before the memory-transfer follow-up

These are build 50 results, not results for the later candidates. Every row uses
actual gameplay or the loaded N-Gage client. Hooked On aggregates all three
10-second windows; its RAM value is the last window. Super Miners and N-Gage
use the successful readiness-controlled repeats. No sampler or forced GC was
active. The raw Sky Force outlier is retained in this table.

| # | Game | Control FPS | Build 50 FPS | Steady PSS MiB, control / candidate | Observed peak MiB, control / candidate |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes | 99.8 | 100.0 | 2098.9 / 1962.2 | 2098.9 / 1962.2 |
| 2 | Sky Force | 315.8 | 224.0 | 2019.1 / 1957.7 | 2020.9 / 1957.7 |
| 3 | K-Rally | 161.8 | 182.2 | 2159.6 / 1892.1 | 2175.6 / 2020.0 |
| 4 | Asphalt 2 | 144.1 | 132.2 | 2138.9 / 1944.3 | 2142.0 / 2086.9 |
| 5 | Hooked On | 242.9 | 249.2 | 3279.9 / 2744.9 | 3279.9 / 2744.9 |
| 6 | Asphalt 3 | 121.0 | 120.6 | 3341.9 / 3063.2 | 3342.0 / 3067.4 |
| 7 | Tetris | 158.3 | 165.3 | 3325.6 / 2992.5 | 3344.4 / 3095.4 |
| 8 | Super Miners | 343.8 | 344.5 | 2077.1 / 1934.7 | 2099.0 / 1969.6 |
| 9 | N-Gage client | — | — | 2603.1 / 2541.7 | 2615.6 / 2567.9 |

Sky Force repeated at 327.0 FPS control, 324.6 build 50 and 324.7 build 49.
The earlier 224 FPS observation is unexplained; its initial scene contains a
Full Health popup absent in the control. It is not evidence of a persistent
29% regression, and it has not been silently removed. K-Rally also varies with
car position and terrain; its earlier slowdown did not persist in later pairs.

Asphalt 2 did show a repeated build-50 deficit. Hardware PMU measurements over
explicit presentation-count endpoints found approximately 0.9% more retired
native instructions and 3.1% more cycles per frame, with different CPU clocks.
These are processor counters, not guest instruction accounting. Build 52 moves
whole-register-transfer misses into precise compiled recovery and reaches
159.4 FPS in the same barrier scene, with 1957.7 MiB steady and 2105.9 MiB
observed peak PSS. A complete build-52 catalogue pass remains outstanding.

Normal paced Asphalt 3 installation takes 31.2 seconds on the public control,
47.6 on build 50 and 42.0 on build 52. These endpoints are the N-Gage installer
thread finishing, not first presentation or actual racing. The unpaced route
timings are not substituted for this normal-launch measurement. The first
visible screen is about 5 seconds in both builds and does not establish that
installation is complete.

Build 53 raises the static worklist/module batch to 64 and avoids reconsolidating
already large cohorts. It reaches 164.2 FPS and installs in 40.7 seconds, but
Asphalt 2 startup peak rises to 2191.9 MiB versus the 2142.0 MiB control. It is
rejected; source returned to build 52 at that point.

The [machine-readable evidence](COMPILED_ONLY_EVIDENCE.json) identifies accepted
captures, artifact hashes, exclusions and the remaining gates.

## Memory-transfer follow-up and historical gates

Build 54 also removes the unreachable scalar callback implementation after a
successful deferred whole-span memory proof. Its SHA-256 is
`fd1a82401ce7b420b7b55216a1396411f7fff252ab7b8ac64844736666f0a938`.
Build 58 adds guest PC, LR, SP and CPSR to unrecoverable memory-fault logging;
its executable SHA-256 is
`806198bb6886b2611c1e218a82ae0f4ee1aea9f2887bdb28f548c2672334c03a`.
Both retain the compiled-only execution path. Neither has been adopted on the
public service.

Fresh paired build-54 checks give 158.1 versus 134.4 FPS for Asphalt 2,
339.0 versus 339.4 for Super Miners, and 158.6 versus 158.5 for Tetris.
Their steady browser PSS savings are about 208, 112 and 390 MiB respectively.
Sky Force measures 312.6 versus 317.5 FPS over its short two-second route,
with about 73 MiB less PSS. All measurements retain their first-window stalls.
The earlier build-54 Super Miners and Tetris observations of 308 and 141 FPS
remain in `final54-catalogue.json`; the paired repeats are `paired54.json`.

K-Rally's three-second driving pair is slower: 145.5 versus 177.4 FPS. A
separate stationary route retains three consecutive ten-second windows:
140.4 / 143.9 / 170.5 FPS candidate and 155.7 / 139.9 / 171.0 control.
The final stationary windows match, but that does not clear the driving
comparison. Both the changing scene and initial compilation need attribution.

The build-54 Asphalt 3 five-second race sample is 82 FPS. A longer build-58
race run records five consecutive five-second windows: 123.6 / 141.1 / 152.0 /
146.7 / 143.4 FPS, versus 117.9 / 129.2 / 136.5 / 139.0 / 129.9 control.
The last three candidate windows compile no additional emulator regions.
Observed browser peak PSS is 2929.0 versus 3214.6 MiB in these longer runs.
The first two windows are retained as warmup evidence; the last three are the
preselected sustained-throughput window, not a selection of the fastest values.

Asphalt 3 has also failed before racing: one build-54 launch returns N-Gage
error -1000002, and another reads null address 0x8 after language selection.
These are distinct failures. The null read has not been found in the inspected
control logs and remains unresolved. Repeated build-58 launches collect the
new fault context; successful gameplay does not erase the failures.

Cold paced installation remains slower. The control repeats at 31.25 seconds,
versus 41.35 for build 54. Removing consolidation entirely reduces installation
to 37.52 seconds but raises Asphalt 2 peak PSS to 2199.5 MiB, above control.
Selective consolidation of small initial modules takes 40.30 seconds and uses
more RAM than build 54. Keeping ROM code for other loaded processes takes
44.18 seconds in its focused run. These alternatives are rejected; source
returns to build 54 plus the fault context.

Chrome startup tracing records 92,955 first-use `wasm.CompileLazy` events
totalling 8.99 seconds in build 52. Module consolidation contributes measurable
startup work, but disabling it does not meet the RAM gate. Trace events do not
identify a module on each lazy compile, so they do not prove which guest
function was compiled twice. Native sampling also distinguishes this browser
compiler work from the emulator's C++ outer loop.

Raw additional evidence is `final54-catalogue.json`, `paired54.json`,
`steady54.json`, `steady58-asphalt-3-candidate/`, and
`asphalt3-stability58.json` under the scratch directory above. Gameplay
screenshots were visually checked, including the N-Gage library, the two
different K-Rally lighting states and the Asphalt 3 racing HUD.

## Production build boundary

Build 59 (`ce4fcc5cc560cfacee5304896d2040917c5d2e3782aa485f5fa84da183d5a66f`)
excludes the four interpreter decoder/translator/loop source files from the
production CPU archive. The standalone WASM test links them directly, with its
own reference-step adapter and matching CPU compile definitions. Binary and
archive inspection pass; the production binary has 13,293 functions and no
interpreter loop, decoder, decoded-storage allocator or reference-step symbol.
The separate oracle passes all 194 cases. The native CPU suite passes 915
assertions in 37 cases. This establishes the build boundary, not adoption.

Three additional fresh build-58 Asphalt 3 launches and three alternating control
launches all reach racing. Together with its earlier extended race capture,
build 58 has four successful launches after the new fault logging. The build-54
null read remains a recorded, unreproduced failure; no causal fix is claimed.

## Startup attribution and further screening

The missing matched control Chrome trace is now captured. Normal paced installer
completion is 32.26 seconds under tracing, versus 42.69 for build 52. The control
has 3,365 synchronous module compilations and 20,703 lazy function compilations;
build 52 has 28,418 and 92,955 respectively. Lazy compilation on the guest worker
totals 2.10 versus 8.87 seconds. ARM-to-WASM translation counters are 5.35 versus
4.82 seconds; module emission is 0.57 versus 1.88, and installation 1.35 versus
4.44. Event durations can nest or run on other threads and are not added into an
exact wall-time decomposition. Lazy events lack module identity, so they do not
prove which individual functions were compiled twice.

The native K-Rally pair also completed. Dispatch takes 485 samples / 616 frames
in the control and 454 / 569 in build 59, approximately 1.4% more samples per
frame. The five hottest generated functions have matching native code sizes.
Diagnostic FPS is 203.7 versus 188.5. Sparse source anchors, different lighting
and the capture extending outside the FPS window prevent assigning the entire
gap to dispatch or compilation. The driving gate is still open.

The following are separate focused screens, not new catalogue passes or speedup
claims. The unchanged control's normal installer endpoint remains 31.19–31.25
seconds. Its recorded paired Asphalt 2 barrier result is 134.4 FPS, 2134.8 MiB
steady and 2142.3 MiB peak PSS.

| # | Build | Approach | Paced install s | Asphalt 2 FPS | Steady / peak PSS MiB |
| --- | --- | --- | ---: | ---: | ---: |
| 1 | 60 | All RAM exports | 47.74 | 125.4 | 1912.7 / 2041.4 |
| 2 | 61 | Imported dependency entries | 51.78 | 127.8 | 2028.5 / 2166.0 |
| 3 | 62 | Cold linear blocks, watchdog next-PC promotion | 41.58 | 107.7 | 1897.2 / 1986.6 |
| 4 | 63 | Control precompilation policy | 48.94 | 124.0 | 2207.7 / 2301.3 |
| 5 | 64 | Cold regions, executed-entry sampling | 43.80 | 137.6 | 1958.2 / 2036.4 |
| 6 | 65 | Background native prewarm | 60.53 | 161.8 | 2078.7 / 2204.7 |
| 7 | 66 | Background module validation | 41.60 | 151.6 | 1918.7 / 2021.2 |
| 8 | 67 | ROM translations survive process exits | 41.53 | 154.0 | 1905.4 / 2045.4 |

None clears the startup gate. Partial eager entry lists overcompile other
regions: the imported-entry screen reaches 169,232 functions, and restoring the
old full policy with the expanded translator reaches 273,950, versus 98,729 in
the traced control. The cold-tier experiments do not recover the desired
throughput/startup combination and are removed.

A separate 3,000-function browser probe shows `WebAssembly.compile()` and
`compileStreaming()` still trigger 3,000 lazy compilations at first call. A
worker can prewarm a cloned module and eliminate those caller-side events, but
applying eager prewarm to real replacement modules takes 60.53 seconds and
exceeds the control memory peak. It warms 95,279 functions during startup. This
is a failed emulator optimization despite the successful synthetic mechanism.
Moving only validation to a worker also produces no startup gain. Both worker
implementations are removed from the current experiment.

Keeping immutable ROM translations removes process-ownership bookkeeping and
preserves correct reuse after exit. The startup screen retains about 11,000
more functions but compiles almost the same total, so these functions were not
being recompiled before installer completion. It does not explain this gap.

Native captures retain raw samples, snapshots and assembly. Their large jitdump
files were losslessly gzip-compressed after analysis; decompress them to rerun
the existing analyzer. A failed module-export capture ran out of disk after
sampling; its native samples were recovered. A later browser-stat collector
waited on blocked workers after the startup endpoint; its completed timing was
preserved and the collector now uses bounded parallel reads. These failures are
not reported as successful profiling or extra startup time.

Detailed local records are `round60-67.json`, `startup-trace-comparison59.json`,
`native59-k-rally-comparison.json`, `async-compile-probe/report.json` and
`worker-prewarm-probe/report.json` beneath the evidence directory above. Frozen
artifacts remain available for reproduction. The public service is unchanged.

## Callback preparation and ROM attribution

Build 68 follows relocated, callable RAM entries recursively. It compiles
149,002 eager functions, installs in 46.03 seconds and reaches 148.1 FPS in the
Asphalt 2 barrier scene. Its 2183.1 MiB observed browser peak exceeds control.
Build 69 prepares these callbacks individually while retaining recursive
preparation for original image entries. Eager functions fall to 8,690, paced
installation takes 41.20 seconds, and Asphalt 2 reaches 162.7 FPS with
1908.8 / 2054.8 MiB steady / peak PSS. This avoids the eager expansion but does
not clear the startup gate.

Build 70 splits the existing compilation totals by ROM and RAM, without adding
guest instruction instrumentation. Paced Asphalt 3 installation takes 41.59
seconds. Of 110,214 translations, 64,475 are ROM (3.708 seconds of C++
translation) and 45,739 RAM (1.203 seconds). Emission takes 1.865 seconds and
installation 4.055 seconds. These totals overlap browser compilation activity
and must not be added to Chrome trace durations as an exact wall-clock split.
Chrome's lazy-compilation events carry function indices without module identity,
so that trace cannot itself identify the ROM share of lazy compilation.

The new focused records are in `focusedRounds68to70` in the evidence JSON.
Neither these screens nor the diagnostic counter split alone justified adoption.

## Build 106 release evaluation

The frozen production WASM for build 106 is
`d31435fa2b15ab989776f661ce7afc6bfb416b953474193adf992bc1f7a2f7dd`.
The incremental restored build matched that SHA-256 during adoption. A later
relink after toggling the allocation profiler did not: the frozen binary has
one diagnostic export, `eka2l1_guest_failure_trace`, absent from the
current source, relink and checked-in callers. Exact bitwise reproducibility
from the current source is therefore not established. The public service
still serves the tested frozen binary; see the
[fresh memory profile](COMPILED_ONLY_MEMORY_PROFILE.md).
The independent WASM interpreter oracle passed 196 tests with no failures;
compiled-only, cache, callback and browser cache-integrity tests also passed.
Native CPU and preserved Asphalt resource suites passed 915/37 and 42/3.

Fresh browser runs reached actual gameplay in all eight supported games and
loaded the N-Gage client. Total browser-process PSS in MiB at the gameplay
checkpoint and observed 1 Hz peak was:

| # | Game | Final control / build 106 | Peak control / build 106 |
| --- | --- | --- | --- |
| 1 | Snakes | 2044 / 1849 | 2052 / 1938 |
| 2 | Sky Force | 2000 / 1943 | 2000 / 1943 |
| 3 | K-Rally | 2131 / 1867 | 2146 / 1990 |
| 4 | Asphalt 2 | 2074 / 1910 | 2080 / 2056 |
| 5 | Hooked On | 3241 / 2684 | 3241 / 2684 |
| 6 | Asphalt 3 | 3191 / 2914 | 3191 / 2977 |
| 7 | Tetris | 3268 / 2924 | 3288 / 3088 |
| 8 | Super Miners | 1941 / 1760 | 1941 / 1972 |
| 9 | N-Gage client | 2466 / 2412 | 2516 / 2450 |

The one Super Miners candidate peak above control is retained. Two more
paired runs gave control peaks 1941 and 1935 MiB versus candidate peaks 1861
and 1833 MiB; this establishes lower typical RAM, not a per-run maximum
guarantee. The savings cannot be assigned solely to removing the interpreter,
because build 106 also changes translation and cache policy.

A 34,448,179-byte ROM translation cache was captured from build 106's Asphalt 3
race. The browser and emulator verify the build, ROM, policy, content digest and
guest source dependencies before using it, falling back to compilation on
mismatch. A normal paced cached launch reached racing with zero ROM
retranslations. Under overlapping host load, installer completion measured
38.97 seconds control and 38.23 seconds cached candidate. The cached candidate
used about 3105 MiB at the race checkpoint versus 3191 MiB control, a smaller
saving than its uncached run. These timings do not prove a reliable startup
advantage, but the original uncached roughly ten-second installation deficit
did not appear in this pair.

Three ten-second K-Rally driving windows measured 84.8 / 71.4 / 162.6 FPS
control and 320.6 / 250.8 / 156.6 candidate. The final windows compiled no
new guest functions and are close, while scene and host activity prevent an
exact throughput claim. Asphalt 3's candidate reached racing in repeated
builds, but longer unpaced runs then displayed a full-version purchase prompt.
The prompt also appeared without the preserved-image source repair, and one
fresh control launch failed earlier with N-Gage error -1000002. Full-version
activation and the exact cause of the prompt remain unverified. Gameplay
throughput and startup stability are therefore release caveats, not resolved
correctness claims.

The public HTTPS launcher serves the exact build-106 SHA from a stable local
release directory. A fresh browser route reached Snakes gameplay at 89.0 FPS
without guest errors. Another reached the Asphalt 3 race with sound enabled at
107.4 FPS, cache status 2, zero ROM retranslations and no guest errors. The
race screenshot shows active game graphics, not the N-Gage client or a menu.
The `.lan` user service is persistent and enabled; its previous control setup
is archived locally for rollback. No game packages or ROM cache blobs were
committed to Git.
