# WasmFS: remove filesystem handoffs to the browser main thread

## Change

The browser build uses Emscripten's in-memory WasmFS backend. C/C++ file
operations execute on their calling worker instead of synchronously proxying
libc metadata, seek, directory and file operations to the main browser thread.
The native frontend is unchanged. There is no new runtime switch or path cache.
Guest clocks, watchdog, compilation policy, games and firmware are unchanged.

The pinned Emscripten 4.0.10 implementation of `FS.writeFile` appends to existing
files. The launcher explicitly opens with `w` and closes first, then uses the
bulk write for nonempty data. This preserves replacement and empty-file behavior
for uploads, N-Gage packages and game-card staging. A chunked `FS.write` prototype
was discarded: its generated JS checks memory views per byte and made large
uploads much slower. The final helper uses the SDK's bulk typed-array copy.

[WasmFS documentation](https://emscripten.org/docs/api_reference/Filesystem-API.html#new-file-system-wasmfs)
describes the worker-local design. SDK behavior above was checked in the actual
4.0.10 `system/lib/wasmfs/js_api.cpp` and generated JavaScript, not inferred from
newer documentation.

## Measurement scope

Source base `e5fb0b00f`, branch `wasm-port`, 2026-10-12. Chromium 153.0.8010.52,
i7-10875H, NVIDIA T1000 ANGLE/Vulkan. Runs are serial without simultaneous own
builds, benchmarks or profiles; ordinary host background work remains enabled.
Both builds use identical served ROM/game files and runtime policies.

Normal startup uses a fresh browser/HTTP cache and guest installation with
warm server assets, excluding Internet/IPFS download latency. N-Gage completion
means successful installer exit plus the **Installation complete** screenshot,
not completion of subsequent menus or readiness to play. First presentation
is a separate counter and can be black. Profiling runs are diagnostic only.

The baseline is the public `admission-build` artifact from the preceding
[startup follow-up](STARTUP_FOLLOWUP_RESULTS.md). The candidate is the frozen
`wasmfs-bulk-build` artifact. Full hashes, per-run measurements, failures and
route programs are in [WASMFS_RESULTS.json](WASMFS_RESULTS.json).

## Fresh installation

| # | Game | Baseline, seconds | Final candidate, seconds |
| ---: | --- | ---: | ---: |
| 1 | Hooked On | 80.29 | 68.86, 72.10; public verification 62.72 |
| 2 | Asphalt 3 | 50.56, 53.89 | 42.44 |
| 3 | Tetris | 47.00 | 36.07 |

Hooked On's fresh control was repeated in this campaign; the other two controls
are the same artifact's preceding campaign results. Thus the observed Hooked On
reduction in the two private previews is 10–14%; the final public verification
is 22% shorter than that control. The smaller samples for the other games are supporting
evidence, not equally strong fresh paired estimates. All final normal-startup
runs completed without a guest/browser error, with completion screens reviewed.

The initial WasmFS-only prototype completed Hooked On in 60.38 seconds, but it
lacked correct overwrite handling and is not the adopted result. The discarded
chunked-write helper took 76.25 seconds and delayed first presentation to 17.24
seconds; the final helper's Hooked On first presentations were 6.72 and 9.42
seconds. These different HTML helpers share the same WASM binary, so the binary
hash alone is insufficient to distinguish them. An initial connection-refused
run happened before the private server was listening and performed no guest
work. Raw results are retained rather than selecting the fastest prototype.

## Profile check

The candidate profiles the page and emulation worker from creation at a 1 ms
CDP sampling interval, without Debugger or coverage instrumentation. The CPU
worker is identified by actual generated guest, `execute_chain` and interpreter
stacks, not just its worker number. Attribution is clipped to receipt of the
successful installer-exit log using profiler start acknowledgements; small RPC
alignment skew is possible. Categories are disjoint sampled elapsed time, not
exact CPU durations or necessarily removable overhead.

| # | Emulation-worker category | Previous focused profile, seconds | WasmFS focused profile, seconds |
| ---: | --- | ---: | ---: |
| 1 | Main-thread filesystem proxy waits | 13.274 | 0 observed |
| 2 | Generated guest self | 13.808 | 15.782 |
| 3 | ARM translation / module preparation subtree | 9.743 | 10.494 |
| 4 | Memory-view maintenance subtree | 6.973 | 9.043 |
| 5 | Compiled dispatch self, including inlined work | 5.600 | 6.204 |
| 6 | Interpreter outer-loop self, including inlined work | 3.039 | 3.558 |
| 7 | Other active runtime work | 9.023 | 6.974 |
| 8 | Sleep / pacing | 11.306 | 10.193 |
| 9 | Graphics / other waits | 0.349 | 0.596 |
| 10 | Startup idle / program samples | 4.881 | 4.562 |

Total candidate interval: 67.41 sampled seconds; installer exit at 67.86 wall
seconds. The preceding focused capture covered 78.00 sampled seconds, with exit
at 78.54 wall seconds. These diagnostic captures confirm the removed handoff,
not a separate unprofiled speedup estimate. No guest/browser or attachment error
occurred. Direct WasmFS execution is still real work: for example `newfstatat`
has a 0.855-second inclusive subtree. It is already included above and must not
be added to the table. Zero sampled proxy waits does not mean zero filesystem
cost, JS calls, or graphics/audio handoffs.

Exact-ROM mapping attributes 9.14 seconds of candidate generated self samples
to `hash.dll`, 3.23 to EUser, 1.12 to EStor and 1.08 to EZLib. These are region
entry attributions and may contain inlined callees. The remaining RAM mapping
gap is 0.204 seconds. The ROM hash is unchanged from the
[baseline profile](HOOKED_STARTUP_PROFILE.md). Other active categories changed
between captures, so subtracting all 13.27 seconds from wall time would overstate
the observed improvement.

## Unpaced gameplay

Each run installs fresh files, enters the existing game-specific scene, then
measures presentations while running continuously in unpaced route phase zero.
Instruction counters remain zero. Guest audio is enabled; browser output is
muted. Screenshots are reviewed, and crashes or wrong menus are rejected before
accepting FPS. These are scene checks, not deterministic frame replays or
confidence intervals. Random pieces, positions, compilation and host noise can
still differ. No profiler runs during the timed samples below.

| # | Game / scene | Baseline FPS | WasmFS FPS |
| ---: | --- | ---: | ---: |
| 1 | Hooked On, Costa Rica boat without tutorial | 182.48 | 208.90 |
| 2 | Asphalt 3, Instant Play race, sound on | 100.76 | 107.12 |
| 3 | Asphalt 2, Golf at San Francisco barrier | 104.81 | 106.80 |
| 4 | Snakes, opening level | 83.20 | 94.98 |
| 5 | Sky Force, stage 1 | 222.30 | 293.26 |
| 6 | K-Rally, tutorial driving | 147.85, 116.60 | 143.82, 144.24 |
| 7 | Tetris, initial level 1 board | 164.26, 134.92 | 140.01, 127.16 |
| 8 | Super Miners, challenge level 1 | 249.45 | 291.87 |
| 9 | Explode Arena, arcade level 1 | 211.61 | 287.04 |
| 10 | Pathway to Glory, Pantelleria battlefield | 139.30 | 132.95 |

The initial windows are 2–10 seconds. Lower candidates trigger targeted repeats,
not automatic promotion from the games that improved. K-Rally repeats overlap
and change direction. Pathway's longer 15-second samples after 10 seconds of
warmup are 133.15 FPS baseline and 136.72 FPS candidate, with gameplay intact.

Tetris's initial candidates are lower. A 10-second warmup / 15-second measurement
attempt gave 149.91 vs 138.26 FPS, but screenshot review found the baseline at the
results screen by the end. That whole-window comparison is rejected and retained
in the JSON. A shorter five-second warmed comparison and separate post-timing
profiles check live gameplay without that scene transition. In baseline/candidate
order the warmed five-second pair is **122.92 / 119.34 FPS**; in the preplanned
reverse-order repeat it is **123.43 / 152.36 FPS** (columns still baseline /
candidate). Both remain on the level-1 board at the endpoints. The large initial
deficit does not repeat consistently in warmed gameplay; these variable samples
do not establish a Tetris speedup or tightly bound small regressions.

Profiles collected only after the first short pair's timing interval spend
86.2% / 86.7% of samples in generated guest code, dominated by the same two RAM
entry PCs, `0x7256d660` and `0x72562880`. There is no new filesystem hotspot.
Inclusive or self function totals cannot prove identical native instruction
cost. The profile screenshots remain in gameplay, and the profile duration is
excluded from the table's FPS. Startup speedup and absence of a consistent warmed
gameplay loss justify adopting the backend; this is not a claim of universal
performance equivalence or that every lower observation was noise.

Asphalt 3 failed on two candidate attempts: one with USER 44 / Domino 44 panics;
the other showed the N-Gage launch dialog “Close some applications” (error
-100002), and subsequent scripted inputs finished at the sound prompt. The
unchanged control also failed a repeat with access violations and KERN-EXEC 3.
These are retained as failed launches, not menu FPS. The successful candidate
has the racing HUD and no guest/browser errors. This campaign does not fix or
establish the cause of the intermittent launch failures, nor prove equal failure
rates from this small sample. A preceding campaign also recorded USER 44 before
the filesystem change.

## Memory and compatibility

WasmFS moves file bytes from JavaScript storage into the WASM linear heap.
Hooked On's gameplay `mallinfo` allocation rises from about 1,525 MiB to 1,866 MiB;
Tetris rises from about 1,522 to 1,821 MiB. These are allocator bytes, not total
renderer RSS, so they do not establish an equal increase in total browser memory.
The existing 2 GiB WASM limit is unchanged. All ten games fit in these desktop
checks, but the remaining linear-heap headroom is smaller. Mobile memory limits
and long sessions are not measured here. Uploaded ROM/RPKG/SIS and card archives
continue to be deleted after extraction; saves remain session-only.

The browser filesystem contract test covers a large upload with a non-page-aligned
tail, binary subarrays, empty replacement, shared file handles, case-preserving
rename, shrink/grow with zero-fill and unlink while a reader remains open. It
passes on the final artifact along with the existing browser API/resource smoke.
The compiler suite passes 188 tests; native CTest passes all three suites.
`git diff --check` passes. A first browser-test invocation could not find
Puppeteer's bundled Chrome; rerunning with `/usr/bin/chromium` passed. This was a
harness setup failure, not a game or filesystem failure.

## Reproduction and artifacts

Raw evidence lives in `/home/claude/.scratch/eka-fs-20261012`: browser logs,
start/end screenshots, sample reports, CPU profiles, harnesses and all failed
attempts. The committed JSON records exact routes, executable/HTML hashes,
per-run results and evidence paths. Baseline profile evidence is in
`/home/claude/.scratch/eka-startup-profile-20261012/hooked-on-focused`.

```sh
source /home/claude/.scratch/eka-benchmark/emsdk/emsdk_env.sh
cmake --build build-wasm --target eka2l1_wasm -j8
PUPPETEER_EXECUTABLE_PATH=/usr/bin/chromium \
  EKA2L1_WASM_BUILD_DIR=/absolute/frozen/build node src/tests/wasm/test.ts
node build-wasm/src/tests/aot/test_aot_wasm.js
ctest --test-dir build --output-on-failure
node src/tests/wasm/startup.mjs URL NEW_OUTPUT hooked-on 180
node /home/claude/.scratch/eka-fs-20261012/runtime.mjs GAME UNIQUE_LABEL URL
```

Candidate WASM SHA-256:
`7f827a1c1810c629329dc32fd587fd85efe10f181e99a229a36be2b150564257`.
Candidate build HTML SHA-256 (before launcher injection):
`ff40407c736e739178c97eb002fef0f7bbd047a7ada73d11ca57fc55d80931a5`.


## Deployment

The normal service now serves `wasmfs-bulk-build` on
<https://claude-laptop.lan:8188/>. Fetching the public WASM with the configured LAN
CA verifies the exact candidate hash above; fetched HTML contains the corrected
bulk staging helper. A fresh public Hooked On installation completes in **62.72
seconds**, first presentation at **6.77 seconds**, with no browser/guest error and
the **Installation complete** screenshot checked. Policy readback confirms the
normal unsafe-code mode 3 and direct-memory mode 2; the watchdog request remains
2,000 us. This is a deployment smoke check, not another gameplay FPS comparison.
The private preview service was stopped afterward. Source changes are local;
no push or game/firmware binary commit is part of this change.
