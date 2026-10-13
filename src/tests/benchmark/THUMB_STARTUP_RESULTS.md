# Thumb compiler startup results

Fresh Hooked On installation now averages **50.90 -> 48.38 seconds** in the
paired startup screen, with elapsed ARM translation **8.23 -> 5.50 seconds**.
That is 4.96% less installation wall time and 33.17% less translation time.
These changes reduce compilation work while preserving generated instructions
and entry discovery. They do not reduce eager compilation coverage.
The [measurement summary](THUMB_STARTUP_RESULTS.json) retains timings,
compilation counters, runtime screens and deployed build identity.

## Changes

1. Remove the instruction-address/index map in `emit_thumb_block`. It was
   populated by a full pre-scan, with a tree allocation per instruction, and
   never read.
2. Skip forward-target collection for bounded translations. That path used to
   build the target set and immediately clear it; unbounded translations still
   collect the same targets in ascending instruction order.
3. Track reachability with one byte per halfword instead of a tree node per
   reachable instruction. The same control-flow walk marks the same starts,
   including wide instructions, branches, terminators and literal-pool skips.
4. Collect broad entry-discovery targets in a contiguous vector, then sort and
   deduplicate it. Its output remains the same ascending, unique target list.

Inlining, region policy, compiler quotas, memory backend, watchdog, guest clock,
instruction accounting and game/firmware bytes retain their previous settings.
There is no new launch switch.

## Profile and attribution

A fresh normal paced installation profile of the unchanged `2ac23b0ea` runtime
uses 1 ms Chrome CDP sampling of the page and executing CPU worker (`worker-13`).
The worker capture spans 51.39 weighted seconds, including 35.62 active seconds.

| # | Sample category | Weighted seconds |
| ---: | --- | ---: |
| 1 | ARM translation / module preparation subtree | 9.246 |
| 2 | Memory-view rebuilding subtree | 7.380 |
| 3 | Other active work | 6.208 |
| 4 | Generated guest code, self | 5.012 |
| 5 | Compiled-chain runner, self including inlined work | 4.761 |
| 6 | Interpreter outer loop, self including inlined work | 3.014 |
| 7 | Parked | 12.635 |
| 8 | Idle | 3.060 |

Compiler work is concentrated in the first profile chunk. Mapping work is
largest in the second. Allocator/free routines also appear among the hot self
frames. There are no observed synchronous filesystem proxy-syscall waits in
this worker capture: both builds already use WasmFS.

These are sampled elapsed weights, including inlined work and profiler
disturbance, not isolated native instruction costs or recoverable wall time.
The optimization is supported separately by dead-pass inspection and controlled
compiler/output comparisons. No profile capture is included in startup means.

## Fresh installation

Hardware: i7-10875H, Chromium 153.0.8010.52, NVIDIA T1000 ANGLE/Vulkan. Every
observation starts with a fresh browser, HTTP/CacheStorage cache and guest
filesystem through the normal paced launcher. Server game/firmware blobs are
warm; Internet/IPFS cold retrieval is excluded. Browser measurements run
serially and do not overlap builds, tests or profiles. Ordinary host background
work remains.

The endpoint is successful N-Gage Installer thread exit. Final screenshots
show **Installation complete**. It excludes subsequent game launch and menus.
Counters are sampled shortly after this endpoint. Translation counters measure
elapsed phase time, including allocator and scheduling time, not isolated CPU.

| # | Run, in measurement order | Installation wall seconds | Translation seconds | Compiled functions |
| ---: | --- | ---: | ---: | ---: |
| 1 | Prescan removals only, exploratory | 49.104 | 6.291 | 98,940 |
| 2 | Unchanged baseline A | 50.722 | 8.207 | 98,920 |
| 3 | Final candidate A | 48.045 | 5.475 | 98,948 |
| 4 | Unchanged baseline B | 51.088 | 8.260 | 98,910 |
| 5 | Final candidate B | 48.718 | 5.531 | 98,938 |

Final means use rows 2-5 only: baseline 50.905 s / 8.234 s translation;
candidate 48.381 s / 5.503 s translation. The initial exploratory run is
retained and excluded from these means. Both final candidate observations
improve both quantities, but two runs per variant do not establish confidence
intervals or the same gain on every device. Small function-count differences
occur with paced execution; admission and discovery rules are unchanged.

## Correctness and gameplay

The original and final Thumb translators were linked together under separate
symbols. **5,000 deterministic stock-ROM window comparisons** match function
bytes, local counts, source marks, outlined call sites and nested bodies,
supported/complete/end metadata, dispatch/literal/resume/branch discovery,
bail counts and dependency addresses/bytes. They cover bounded/unbounded,
stop-after-store and register-cache combinations with source marks enabled.
The prescan-only and bitmap-only intermediate stages also pass this oracle.
This is sampled compiler equivalence, not full-game module equivalence.

All **188 ARM/Thumb WASM tests** pass, as do source-map relocation/pruning/module
checks and all three native CTest suites.

The unpaced comparison uses the same guest clock, memory backend, watchdog,
assets, renderer and continuous route on both artifacts. Hooked On uses the
same installed-files image; Snakes installs normally. Screenshots at the start
and end show the tutorial-dismissed Costa Rica boat and live opening Snakes
gameplay. Guest audio remains enabled; the harness mutes browser output.
Instruction totals remain zero and no guest/page errors are reported.

| # | Scene | Baseline FPS | Candidate FPS | Window seconds |
| ---: | --- | ---: | ---: | --- |
| 1 | Hooked On, stationary Costa Rica boat after tutorial | 237.62 | 239.02 | 10.10 / 10.11 |
| 2 | Snakes, opening level | 98.22 | 98.51 | 5.03 / 5.03 |

These are presentation-rate regression screens, not frame-exact replays or a
new Qt comparison. Hooked On compiles no new functions in either timed window;
its translation counters include 104 / 512 microseconds of unsuccessful or
cached attempts. Snakes still performs a small amount of compilation during
the short window. The evidence finds no runtime loss in these two scenes; it
does not establish a runtime gain or unchanged performance for all ten games.

## Normal paced image launch and deployment

A fresh normal auto-launcher check uses the existing Hooked On installed-files
image, with the baseline runtime first and final candidate second. OCR-guided
inputs reach the Costa Rica welcome, dismiss it and show the actual boat.
Movement followed by animation verifies live gameplay. This endpoint includes
restoration, compilation, logos, prompts and menus; it is not installer exit.

| # | Runtime | Client ready seconds | Actual gameplay seconds | Paced gameplay FPS |
| ---: | --- | ---: | ---: | ---: |
| 1 | Unchanged baseline | 17.318 | 68.033 | 31.67 |
| 2 | Final candidate on public `.lan` | 14.855 | 63.923 | 31.78 |

The single pair reaches gameplay 4.11 seconds earlier. OCR polling and host
timing contribute variability, so this is a launch screen rather than a
confidence interval. Both ten-second gameplay windows run at approximately
one guest second per host second, with zero instruction totals and no errors.
The candidate's reviewed images show the boat after the welcome is dismissed,
not just an installed N-Gage client.

`eka2l1-lan.service` serves the frozen candidate at
`https://claude-laptop.lan:8188/`, retaining the existing image directory.
Downloaded WASM bytes match the candidate SHA below. A subsequent fresh normal
Snakes launch checks the public fallback installation path: its reviewed logo
screen has 136 presentations, zero instruction totals, the normal memory/code
policies and no guest/page errors. Private comparison servers are removed after
verification.

## Artifacts and reproduction

Source base is `2ac23b0ea` on `wasm-port`, plus the changes committed with this
report. Frozen build identities:

| # | Artifact | WASM SHA-256 |
| ---: | --- | --- |
| 1 | Unchanged baseline | `176bef9bbb8f0899f8538b60978a0674a30e95b9cec32ddb015dc1cd10cd39ca` |
| 2 | Prescan removals only | `e6c0ff49160b7cc394a81ade7777e3e02ebc1a58361324d27e0ae8c2ba1424bb` |
| 3 | Final candidate | `6c29da65efd23d393f3981c4ab50123ffd78115f88f09ae56eb814d5c7e88437` |

Raw evidence is `/home/claude/.scratch/eka-install-opt-20261013`: startup
reports/screenshots/logs, profile chunks and aggregation, runtime routes and
screenshots, original translator and separate-symbol oracle, frozen builds and
build/test logs. The first profile attempt raced server startup and failed to
connect; it is excluded. Failed initial artifact-copy commands produced no
measured artifact. No game/firmware/image binary is committed.

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm test_source_maps -j4
node build-wasm/src/tests/aot/test_aot_wasm.js
node build-wasm/src/tests/aot/test_source_maps.js output.wasm
cmake --build build --target ekatests eka_cpu_tests -j4
ctest --test-dir build --output-on-failure
node src/tests/wasm/startup.mjs https://claude-laptop.lan:8188/ /absolute/new-output hooked-on 120
```

For the raw installer path, serve a frozen build without
`EKA2L1_PREINSTALLED_DIR`; an enabled Hooked On image deliberately skips that
installer and measures a different endpoint. The remaining profiled startup
work includes mapping maintenance, guest execution and pacing. Removing
compiler bookkeeping does not eliminate those costs.
