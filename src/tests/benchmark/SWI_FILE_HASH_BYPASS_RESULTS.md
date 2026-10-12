# SWI file-hash bypass and deployed startup profile

Hooked On's fresh N-Gage installation takes **51.53 seconds on average** with
the file-hash check bypassed, versus **62.26 seconds** for the previous WasmFS
build: a **17.23% reduction** in this four-run campaign. The candidate is served
at `https://claude-laptop.lan:8188/`; its downloaded WASM hash matches the tested
artifact. No additional runtime optimization was adopted during profiling.

## Scope and caller proof

The default EKA2 library patch returns true from
`securitymanager.dll::Swi::CSecurityManager::VerifyFileHashL` (export ordinal 6).
It applies to the shared Qt/WASM backend for Symbian 9.1 and later and adds no
launch switch. It changes the loaded library export through the existing HLE
patch mechanism; the firmware and game package files remain unchanged.

Debugger snapshots of guest registers and saved return addresses, followed by
ROM disassembly, establish this installation path:

```text
N-Gage installer → InstallServer
  → CInstallationProcessor::CheckHashL
    → CSecurityManager::VerifyFileHashL
      → CalculateHashLC → hash.dll::CSHA1::Update / DoUpdate / Block
```

`CheckHashL` constructs a temporary file provider, consumes the Boolean result
and destroys the provider. The original implementation rereads the extracted
file in 1 KiB chunks and compares its SHA-1 with the package digest. The bypass
does not read or alter the guest arguments. It skips verification of installed
file contents, so a corrupted or modified package file can pass this check.
`hash.dll`, `CalculateHashLC` and certificate/signature verification retain
their implementations; other observed SHA-1 callers include X.509/PKIX code.

The corresponding primary sources are the Symbian
[security manager implementation](https://github.com/SymbianSource/oss.FCL.sf.mw.appinstall/blob/master/installationservices/swi/source/securitymanager/securitymanager.cpp),
[installation processor](https://github.com/SymbianSource/oss.FCL.sf.mw.appinstall/blob/master/installationservices/swi/source/swis/server/installationprocessor.cpp)
and [export definitions](https://github.com/SymbianSource/oss.FCL.sf.mw.appinstall/blob/master/installationservices/swi/eabi/SecurityManager_v2U.DEF).
The served stock ROM has SHA-256
`89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490`.

## Unprofiled startup comparison

Captured on 2026-10-12, source base `7b09cc8bf` on `wasm-port`. Chromium
153.0.8010.52, i7-10875H, NVIDIA T1000 ANGLE/Vulkan. Normal paced launcher,
fresh browser/HTTP cache and guest installation, warm server assets;
Internet/IPFS retrieval is excluded. Runs are serial with no concurrent own
builds, benchmarks or profiles. Ordinary host background work remains enabled.

The endpoint is receipt of a successful installer-exit log, with the
Installation complete screen reviewed. It is **not gameplay readiness**.
First presentation can be a black frame or the N-Gage client. The fixed order
was baseline, candidate, candidate, baseline.

| # | Build | Installation wall seconds |
| ---: | --- | ---: |
| 1 | WasmFS baseline | 60.008 |
| 2 | Hash bypass | 50.960 |
| 3 | Hash bypass | 52.094 |
| 4 | WasmFS baseline | 64.505 |

Both candidate runs are faster than both baseline runs. There are only two
samples per build; this is an observed improvement, not a confidence interval.
All four runs completed with no guest/browser errors. Policy remains a
2,000-us watchdog, unsafe-code mode 3 and memory mode 2; instruction totals
remain zero. The baseline and candidate data and HTML artifacts are identical.

## Fresh profile of the public build

CDP Profiler samples the page and emulation worker at 1 ms from their creation,
without Debugger or coverage. A fresh normal-launcher capture on the public
`.lan` URL completes installation at **52.679 seconds**, with no guest/browser
or profiler attachment errors. Attribution is clipped to the installer-exit
log; profile clock alignment uses the start acknowledgement and can have small
RPC skew and rotation gaps.

The CPU worker has 52.305 sampled elapsed seconds, including 36.335 seconds
in active categories. These disjoint categories are **sample-weighted elapsed
time**, not exact CPU durations or guaranteed removable startup delay.

| # | Emulation-worker category | Sampled seconds | Share of active samples |
| ---: | --- | ---: | ---: |
| 1 | ARM translation / module preparation subtree | 9.789 | 26.9% |
| 2 | Memory-view maintenance subtree | 7.654 | 21.1% |
| 3 | Other active runtime work | 6.119 | 16.8% |
| 4 | Generated ARM translations, self | 5.078 | 14.0% |
| 5 | `execute_chain` self, including inlined work | 4.695 | 12.9% |
| 6 | Interpreter outer-loop self, including inlined work | 3.000 | 8.3% |

Excluded from the active denominator are 12.670 seconds parked (12.410 in
sleep/pacing), 3.252 idle and 0.048 program samples. No main-thread filesystem
proxy waits are observed. Filesystem work and JS calls still exist.

`hash.dll` generated-code self samples fall from **9.192 seconds** in the
[previous profile](HOOKED_POST_WASMFS_PROFILE.md) to **0.021 seconds**. A separate
private candidate profile also records only 0.022 seconds. This demonstrates
that the targeted bulk hashing disappeared; the unprofiled campaign measures
the startup gain separately.

| # | Guest ROM module | Generated-code self seconds |
| ---: | --- | ---: |
| 1 | EUser.dll | 2.384 |
| 2 | EStor.dll | 0.868 |
| 3 | EZLib.dll | 0.746 |
| 4 | EDbms.dll | 0.218 |
| 5 | Unresolved entries | 0.183 |
| 6 | hash.dll | 0.021 |

About 99% of sampled compiler work is in the first profile chunk. Its named
leaf `state_local_cache::finish` takes 2.102 seconds self; this is code generation,
not runtime register writeback. Final counters record 8.743 seconds of
translation, 0.577 of emission and 1.365 of installation. Counters overlap
sampled compiler work and can include preparation outside this worker.

Memory-view maintenance becomes the largest category in the next chunk
(4.971 seconds). The MMU memory callback has 7.546 seconds self including
inlined memory-view work. Its total does not isolate generation checks,
mapping enumeration, publication or clearing costs. Compilation and mapping
are the remaining investigation targets. EUser attribution alone does not
identify an allocator, and outer-loop self samples cannot all be labeled
removable dispatch overhead.

## Verification and evidence

The WASM executable and Qt/test targets build successfully. CTest passes all
three suites: `ekatests`, `eka_cpu_tests`, `bluetooth_netplay`.

| # | Focused Chrome gameplay check | Unpaced presentations/s | Result |
| ---: | --- | ---: | --- |
| 1 | Hooked On baseline, Costa Rica boat with tutorial dismissed | 223.80 | 10.12 s, no errors |
| 2 | Hooked On candidate, same scene | 227.31 | 10.13 s, no errors |
| 3 | Tetris candidate, Marathon level 1 | 172.49 | 5.05 s, no errors |
| 4 | Asphalt 3 candidate, Instant Play with guest sound enabled | 152.08 | 5.06 s, no errors |

Hooked On's short pair shows no observed throughput loss in that window; it is
not a deterministic replay or evidence of a runtime speedup. Tetris and Asphalt
3 are fresh-install compatibility checks without paired baselines. Browser
output is muted while guest audio remains enabled. This is not a ten-game
regression rerun, and these rates are not paced gameplay FPS.

Tetris screenshots show a falling piece on the level-1 board, then placed blocks
and score 36. After Installation complete, the left softkey (web **L**, keyboard
**F1**) activates Start Game. Select/Enter chooses Single-player and Marathon;
L/F1 advances the tutorial's Next pages, then Select/Enter enters the board.

Two automated Asphalt 3 routes stopped at menus and failed the racing-HUD
assertion; neither is counted as a gameplay pass. Visually guided input then
selected English, confirmed sound with the left softkey, chose Instant Play,
passed the HUD assertion and completed the compatibility window. Failed route
evidence remains in the JSON.

The live frozen build uses WASM SHA-256
`e715734a51b115f529727070a56147124b7544ac1f2ef1245aa98eb1e8ccfa67`.

[SWI_FILE_HASH_BYPASS_RESULTS.json](SWI_FILE_HASH_BYPASS_RESULTS.json) records
the measured runs, profile totals, policy, asset hashes and raw evidence
checksums. Raw logs, profiles, screenshots and runnable scratch harnesses are
retained under `/home/claude/.scratch/eka-swi-hash-bypass-20261012/`; caller proof
snapshots are in `/home/claude/.scratch/eka-hash-caller-research-20261012/`.

## Fresh repeat on the unchanged public build

A second campaign from source base `b94980498` repeats the profile and checks
the three previously slowest automatic loading stages. The downloaded public
WASM, launcher JS, data, ROM, RPKG and Hooked On packages match the preceding
capture. The compiler policy is unchanged and instruction totals remain zero.
All runs are serial, with no concurrent own build or benchmark, on the same
Chrome/ANGLE/Vulkan host. Screenshots confirm Installation complete; no
guest/browser errors occur. These endpoints still exclude manual game launch
and navigation into gameplay.

| # | Game / capture | Fresh-install wall seconds | First presentation seconds |
| ---: | --- | ---: | ---: |
| 1 | Asphalt 3, unprofiled | 44.342 | 6.590 |
| 2 | Tetris, unprofiled | 43.587 | 12.808 |
| 3 | Hooked On, profiled | 76.231 | 13.338 |
| 4 | Hooked On, subsequent unprofiled control | 51.511 | 6.644 |

Hooked On remains the slowest of these fresh checks. Its unprofiled result
matches the previous two-run mean of 51.527 seconds. The other seven games
were not rerun; their preceding automatic startup stages were much shorter.

The profiler capture is unusually slow compared with both the preceding
52.679-second profile and its subsequent unprofiled control. Its compiler
counters record 19.850 seconds translation, 1.361 emission and 4.348
installation, versus 8.645, 0.565 and 1.370 in the unprofiled control. The final
compiled-function totals are similar (98,922 versus 98,958). Host load changes
across the serial runs: one-minute load is 5.59 before the profile and 2.73
before the control. These observations do not separate sampling overhead,
browser tiering and host contention. This capture supplies hotspot evidence;
its longer wall time does not establish a runtime or startup regression.

Attribution is clipped to successful installer exit using the same 1-ms CDP
sampling and profiler-clock alignment as before. The worker contains 75.831
sampled elapsed seconds, with 59.610 seconds in active categories. These are
sample weights, not exact CPU durations or guaranteed removable delays.

| # | Emulation-worker active category | Sampled seconds | Active share |
| ---: | --- | ---: | ---: |
| 1 | ARM translation / module preparation subtree | 24.448 | 41.0% |
| 2 | Memory-view maintenance subtree | 9.014 | 15.1% |
| 3 | Other active runtime work | 8.848 | 14.8% |
| 4 | `execute_chain` self, including inlined work | 7.060 | 11.8% |
| 5 | Generated ARM translations, self | 6.357 | 10.7% |
| 6 | Interpreter outer-loop self, including inlined work | 3.884 | 6.5% |

Excluded from the active denominator are 11.036 seconds parked (10.607 in
sleep/pacing), 5.068 idle and 0.117 program samples. No main-thread filesystem
proxy waits are observed. The page contains 3.117 active sampled seconds;
background browser compiler threads are not separately sampled.

Compiler work dominates the early chunk: 22.901 of its total 24.448 sampled
seconds occurs there. The largest named compiler leaf is
`state_local_cache::finish`, at 5.026 seconds self; `emit_thumb_block` has another
2.398 seconds self within the same compiler subtree.
[finish](../../emu/cpu/include/cpu/aot/state_locals.h) constructs register
transfer code, copies emitted code and source marks, relocates call offsets
and prunes transfers. This is translator work, not guest runtime register
writeback. The unprofiled translation counter of 8.645 seconds confirms a
substantial early compilation phase outside the distorted capture too;
counters and sample totals overlap and must not be added.

The MMU callback has 8.910 seconds self including inlined memory-view work.
The containing memory category dominates the final chunk with 5.220 seconds.
[The callback](../../emu/mem/src/mmu.cpp) supplies mappings to
[view::enter](../../emu/cpu/src/aot/memory_experiment.cpp), which clears the old
published pages, enumerates current bindings, publishes them and checks the
direct arena when generation or address space changes. The profile cannot
isolate the cost of each operation. Earlier rejected mapping caches remain
rejected; no new mapping implementation was tested in this repeat.

Mapped guest self samples put EUser.dll at 2.981 seconds, EZLib.dll at 1.007
and EStor.dll at 0.961. **hash.dll remains negligible at 0.027 seconds**;
the file-hash bypass still removes bulk hashing. EUser's entire attribution
is not allocator evidence, and outer-loop samples include actual interpreted
guest work as well as inlined runtime work.

The independent OS bracket covers first presentation through the final
screenshot: 64.571 seconds elapsed, 47.132 CPU seconds on the busiest renderer
thread and 77.180 renderer CPU seconds across threads. This is a different
window, and OS thread identity is not directly linked to the CDP worker;
these figures cannot replace or be added to the sampled categories.

The repeat reinforces **compiler preparation first and memory-view maintenance
second** as the startup targets. Compiler share varies from 26.9% to 41.0% of
active samples across the two public captures; exact production cost or a
speedup prediction needs a controlled implementation comparison.

Raw evidence is retained under
`/home/claude/.scratch/eka-startup-reprofile-20261012/`. The JSON's
`startup_profile_repeat` section records the runs, provenance, asset checks,
categories, leaf attribution, counters and evidence checksums. ROM-annotated
CPU profiles in `hooked-on/annotated/` open directly in Chrome DevTools;
sample weights, timestamps and node identities are preserved. Labels identify
region-entry modules and offsets, not individual ARM instructions; RAM entries
without load-lifetime metadata remain unresolved.
