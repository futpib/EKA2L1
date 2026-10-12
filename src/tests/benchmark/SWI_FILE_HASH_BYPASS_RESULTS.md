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
