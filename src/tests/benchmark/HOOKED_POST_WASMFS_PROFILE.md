# Hooked On startup profile after WasmFS adoption

The deployed WasmFS build still has no sampled main-thread filesystem proxy
waits. Compilation, guest hashing and memory-view maintenance remain substantial,
with different costs dominating different phases of installation.

This is a profiling repeat. Runtime code, configuration and deployment are
unchanged. The [WasmFS adoption report](WASMFS_RESULTS.md) contains unprofiled
startup measurements and the ten-game gameplay regression checks.

## Capture and validity

Captured on 2026-10-12 from `https://claude-laptop.lan:8188/`, checkout
`578ccc614` on `wasm-port`. The downloaded WASM matches the tested, deployed
WasmFS artifact, SHA-256
`7f827a1c1810c629329dc32fd587fd85efe10f181e99a229a36be2b150564257`.
The policy readback confirms the normal 2,000-us watchdog, unsafe-code mode 3
and memory mode 2. Instruction totals remain zero.

Chromium 153.0.8010.52, i7-10875H, NVIDIA T1000 ANGLE/Vulkan; normal paced
launcher, fresh browser/HTTP cache and guest installation, warm server assets.
Internet/IPFS retrieval is excluded. Runs are serial without concurrent own
builds, benchmarks or profiles; ordinary background host work remains enabled.

CDP `Profiler` samples the page and emulation worker from creation at 1 ms,
without Debugger or coverage instrumentation. The worker is identified by
actual `execute_chain`, interpreter and generated guest stacks. There are no
guest/browser or profiler attachment errors. Successful installer exit occurs
at **63.95 seconds**, and the final screenshot confirms **Installation complete**.
First presentation is 6.85 seconds; it is not gameplay readiness.

## Remaining costs

Attribution stops at receipt of the successful installer-exit log. These are
disjoint **sample-weighted elapsed seconds**, not exact CPU durations or
guaranteed removable delays. Named examples below overlap their containing
categories and must not be added to this table.

| # | Emulation-worker category | Sampled seconds | Share |
| ---: | --- | ---: | ---: |
| 1 | Generated ARM translations | 14.519 | 22.8% |
| 2 | Sleep / pacing | 11.997 | 18.9% |
| 3 | ARM translation / module preparation subtree | 10.522 | 16.5% |
| 4 | Memory-view maintenance subtree | 7.396 | 11.6% |
| 5 | Other active runtime work | 6.371 | 10.0% |
| 6 | `execute_chain` self, including inlined work | 5.726 | 9.0% |
| 7 | Interpreter outer-loop self, including inlined work | 3.419 | 5.4% |
| 8 | Startup idle / program samples | 3.257 | 5.1% |
| 9 | Graphics / other waits | 0.395 | 0.6% |
| 10 | Main-thread filesystem proxy waits | 0 observed | 0 observed |

Total: 63.602 sampled seconds; active categories total 47.954 seconds. Profiler
clock alignment uses the start acknowledgement, so small RPC skew is possible;
profile rotations also leave small gaps. Raw profiles retain post-completion
screenshot/polling time, which is excluded from the attribution above.

Direct filesystem work remains, including `newfstatat` (0.743 seconds inclusive)
and its overlapping WasmFS path lookup (0.731 seconds). `fd_seek` is 0.045
seconds inclusive. These examples do not measure all filesystem work, but
confirm the old synchronous proxy wait is no longer the startup bottleneck.
Zero sampled proxy waits does not mean zero filesystem cost or JS calls.

### Guest execution: hash.dll remains dominant

ROM entries are mapped against the exact served ROM, SHA-256
`89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490`.

| # | Guest module | Generated-code self seconds |
| ---: | --- | ---: |
| 1 | `hash.dll` | 9.192 |
| 2 | `EUser.dll` | 2.588 |
| 3 | `EStor.dll` | 0.892 |
| 4 | `EZLib.dll` | 0.750 |
| 5 | `EDbms.dll` | 0.209 |

`hash.dll` accounts for 63.3% of generated-code self samples and 19.2% of active
worker samples. This repeats the previous WasmFS capture's 9.139 seconds.
The unresolved 0.198 seconds comprise 0.168 in RAM versions and 0.030 in ROM
entries without an image mapping. Entry attribution may include inlined
callees; it does not establish the exact hash algorithm or the cost of each
ARM/native instruction. Improving its generated code is a concrete investigation
target; replacing guest hashing would need separate correctness evidence.

### Compilation first, hashing next, mapping late

The three worker profile chunks expose distinct phases. Boundaries are profile
rotations, not semantic installation stages; the final chunk is clipped to exit.

| # | Approximate wall interval | Sampled seconds | Compilation subtree | hash.dll self | Mapping subtree |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | 0.17–28.23 s | 28.060 | 10.446 s | 0.066 s | 1.528 s |
| 2 | 28.34–49.81 s | 21.470 | 0.007 s | 8.551 s | 1.900 s |
| 3 | 49.88–63.95 s | 14.073 | 0.069 s | 0.575 s | 3.968 s |

About 99.3% of sampled compiler/module-preparation work occurs in the first
chunk. Its largest named compiler leaf is `state_local_cache::finish` at
2.278 seconds self; this is code generation, not runtime register writeback.
Final counters record 9.322 seconds of translation, 0.615 of emission and 1.492
of installation. These counters include preparation outside the profiled
worker and overlap the sampled compiler subtree; they cannot be added to it.

The largest named native runtime leaf is the memory callback installed in
[mmu.cpp](../../emu/mem/src/mmu.cpp), at 7.300 seconds self. It includes inlined
work in [memory_experiment.cpp](../../emu/cpu/src/aot/memory_experiment.cpp):
check mapping generation/address space, enumerate bindings on changes, clear
old published mappings, publish current mappings and validate the direct arena.
The 7.396-second containing subtree does not identify exactly how much each
step costs. Its final-chunk share is 28.2%. This is the clearest remaining
emulator-runtime target for native-path attribution. Earlier rejected mapping
caches remain rejected; this capture supplies no new regression evidence for
adopting them.

`execute_chain` and `InterpreterMainLoop` also contain inlined runtime work;
the latter includes actual interpreted guest execution. Their totals must not
be labeled entirely removable dispatch/check overhead. Pacing waits are part
of this normal paced capture, not evidence of wasted filesystem time.

## Repeatability and next target

| # | Focused capture | Installation wall seconds | Compiler sampled seconds | hash.dll sampled seconds | Mapping sampled seconds | Filesystem proxy sampled seconds |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Legacy JS filesystem | 78.54 | 9.743 | 8.685 | 6.973 | 13.274 |
| 2 | First WasmFS profile | 67.86 | 10.494 | 9.139 | 9.043 | 0 observed |
| 3 | Deployed WasmFS repeat | 63.95 | 10.522 | 9.192 | 7.396 | 0 observed |

Hashing and compiler cost repeat closely; mapping and other work vary. These
diagnostic wall times are not a paired unprofiled speedup estimate. No new code
change occurred between the two WasmFS profiles.

For emulator-runtime overhead, examine the mapping callback's actual native
path next. For early startup, prepared ARM translations address the measured
compiler work, while browser module compilation/instantiation still remains.
A preinstalled filesystem could avoid the repeated N-Gage installer, including
hashing and much of its mapping churn; its download/deserialization costs need
measurement. No prepared image or further optimization was implemented here.

## Evidence

[HOOKED_POST_WASMFS_PROFILE.json](HOOKED_POST_WASMFS_PROFILE.json) contains
provenance, asset hashes, category and phase totals, policy, operation counters,
comparison captures and evidence checksums. Raw profiles, screenshots, logs
and scripts are retained under
`/home/claude/.scratch/eka-post-wasmfs-profile-20261012/`.

The `hooked-on/annotated-worker-13-part-*.cpuprofile` copies can be loaded in
Chrome DevTools with ROM region names replaced by module plus offset and the
original generated entry name. Samples, timestamps and node identities are
unchanged; original profiles remain alongside them. RAM entries without mapping
lifetime metadata stay unresolved.

The independent Linux scheduler bracket covers first presentation through the
final screenshot: 60.23 seconds elapsed, 44.91 CPU seconds on the busiest
renderer worker, and 67.23 renderer CPU seconds across all threads. It has a
different window and includes browser compiler/rendering threads. OS thread
identity is not directly linked to CDP worker identity, so these values must
not be substituted for or added to the sampled attribution.
