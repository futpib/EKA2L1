# Asphalt 2: matched paced Qt/WASM benchmark and profile

Measured 2026-10-11, checkout `1e1922ee9` on `wasm-port`. In the same car, track
and settled barrier scene, **Qt averaged 17.23 FPS and WASM 10.96 FPS**, an
observed **36.4% WASM deficit**. Native profiling identifies compiled-region
handoffs as the largest emulator overhead: **42.7% of CPU-worker samples**.
No emulator source or deployment change was made in this investigation.

Follow-up: [frame/timer tracing and expanded native disassembly](ASPHALT_FRAME_TIMELINE.md)
now distinguish the larger execution cost from an additional browser display
wait. The scheduling uncertainty discussed below describes this original capture.

## Fixed workload and timing

The [ten-game comparison](QT_VS_WASM_PACED_FPS.md) used randomized Instant Play
and different vehicles/routes. This follow-up uses the repeatable scene from the
[framebuffer-loop experiment](ASPHALT_FINITE_LOOP_RESULTS.md): original
Gameloft-signed Asphalt 2 1.0.1 N95 8GB SIS, stock Nokia 5320 RM-409, Arcade /
Single Race / red Volkswagen Golf GTI Mk V / San Francisco. Apply no steering or
nitro and wait until the car settles against the first outside barrier. Confirm
the scene visually; the old 20-second warmup was for **unpaced** execution.

Both frontends ran normally paced. WASM used the deployed count-free build,
2 ms watchdog and deadline-aware idle wait. Qt used Dynarmic and the same native
binary as the ten-game comparison. FPS counts screen presentations per host
second, including repeated images. The car nudges along the barrier, so this is
matched gameplay state, not pixel-exact replay.

Only one emulator ran at a time. The accepted window order was WASM A, Qt B,
Qt C, WASM B. Owned WASM renderers were suspended during Qt execution and given
three seconds to recover afterward. No profiler or debugger was attached during
these timing windows. Start/end screenshots were inspected for each sample.

| # | Window | Frames / host seconds | FPS | Guest / host time | Emulator compilation |
|---|---|---:|---:|---:|---:|
| 1 | WASM A | 327 / 30.23432 | 10.82 | 1.000019 | 17.199 ms |
| 2 | Qt B | 513 / 30.00848 | 17.10 | Native paced | Not instrumented |
| 3 | Qt C | 521 / 30.00787 | 17.36 | Native paced | Not instrumented |
| 4 | WASM B | 336 / 30.23125 | 11.11 | 0.999985 | 5.147 ms |

Aggregate rates use total frames divided by total host seconds. Qt windows each
contained exactly one game drawable. WASM instruction totals remained zero and
route phase remained -1; no page/guest access faults were recorded. Its busiest
renderer thread used 14.33/30.30 and 14.12/30.37 CPU seconds, about 47% of one core.
Guest-requested waiting can coexist with expensive work on each frame's critical
path, so average utilization alone does not rule out rendering overhead.

Hardware was an Intel i7-10875H. Qt used accelerated GLX on Intel UHD under native
Xorg; Chromium 153.0.8010.52 used accelerated ANGLE/Vulkan on NVIDIA Quadro T1000
Max-Q. Host display scaling also differed. Other workloads and power settings
were left alone, and CPU frequency varied. These short shared-host samples
establish a deficit in this scene, not an isolated-host confidence interval.
Qt audio used a private null sink and browser output was muted; audio correctness
was not tested.

## Chrome profile: dispatch rather than compilation

A separate 20-second CDP capture sampled the page and workers at 1 ms intervals.
Linux scheduler counters independently measured actual CPU consumption. Excluding
parked CPU-worker samples, `execute_chain` and its inlined work accounted for
**46.49%**, generated regions **44.00%**, uncached trusted lookup **2.53%**, the
outer interpreter loop **1.42%**, and audio resampling **1.31%**.

The CPU worker was about 68.2% parked in the elapsed-time profile, predominantly
under idle sleep, and consumed 6.59 CPU seconds in the 20.19-second counter window.
The graphics worker was about 98.6% parked and used 0.29 CPU seconds. Parked time
is not all removable overhead; sustained graphics-worker CPU load is not the
main bottleneck shown here. Emulator compilation totaled only 21.926 ms.

Profiling all isolates added sampling threads and changed observed timing. Its
FPS is excluded from the ordinary timing comparison.

## Native profile: where the dispatcher spends CPU time

A fresh diagnostic browser used V8's JIT log and Linux `task-clock:u` sampling at
1 ms intervals. Sampling took place in the verified Golf/barrier scene. It
captured 13,842 samples across renderer threads, including **12,508 on the CPU
worker**, with zero losses, errors, new threads or missing threads. The existing
V8 15.3.76.13 metadata adapter found all 100 selected code objects. Module capture
through the debugger happened only after sampling and metadata collection.

`execute_chain` contains **5,345 samples (42.73%)**, versus **5,364 (42.88%)** in
all generated guest functions. The following disjoint native ranges explain most
of the dispatcher total; percentages use all CPU-worker samples as denominator.

| # | Native path inside `execute_chain` | Native offsets, end exclusive | Samples | CPU-worker share |
|---|---|---|---:|---:|
| 1 | V8 indirect-call target setup, checks and call | `0xc62..0xcc8` | 1,996 | 15.96% |
| 2 | RAM code-cache key construction and cached-entry checks | `0xe64..0xf12` | 1,785 | 14.27% |
| 3 | Post-region stop, IRQ and watchdog checks | `0xcd0..0xd45` | 633 | 5.06% |
| 4 | Next guest PC normalization and ROM/RAM selection | `0xd45..0xd9f` | 522 | 4.17% |

The first range contains table bounds/type/target checks and an indirect call.
The second loads the mapping generation, constructs the address-space/PC key,
hashes the dispatch slot, and checks the slot's key, generation, mapping source
and function. It is the fast lookup path, not executable-byte scanning. The
separate uncached lookup function adds 413 samples (3.30%). These handoffs execute
within WASM; they are not JavaScript calls.

Ranges were classified by reading the captured x86 instructions and exact V8
source anchors. This does not assign intrinsic latency to an individual `mov`
or branch: sample skid and preceding memory stalls can affect the sampled IP.
Source positions are sparse: only 2,006 of 12,508 CPU-worker samples have exact
C++/ARM instruction attribution. Function identities and captured native basic
blocks provide the broader classification; unmapped samples remain explicit.
This profile's FPS is diagnostic and excluded from the Qt/WASM comparison.

## Guest loops and the next experiment

The already optimized framebuffer loop at `0x700019c4` is only **3.87% of active
CDP samples**, or 420 native samples (3.36%). Other hot entries include
`0x70004c6e`, `0x70004c94`, `0x70007152` and `0x70033c00`, all in
`asphalt2_full.exe` at base `0x70000000`. Disassembly shows texture/pixel lookup,
conditional transparent-pixel handling, small color helpers, and cursor updates.
These are instruction-derived interpretations, not recovered source symbols.
The original executable's decompressed code matches the previous framebuffer
experiment byte-for-byte.

[RAM compilation](../../emu/cpu/src/aot/aot_runtime.cpp) selects
`stop_after_store=true` for Thumb. The [translator](../../emu/cpu/src/aot/thumb_translator.cpp)
normally ends the region after a store; its finite-countdown-loop recognizer is
an exception. Its existing region-fusion path excludes this RAM configuration.
For example, `0x70004c6e` contains a conditional skip, a call to the color helper
at `0x70037ea4`, a pixel store and stack-held cursor updates. It does not fit the
current straight-line `SUBS/BNE` termination proof. These boundaries repeatedly
pay state publication, target lookup, checks and another indirect call.

The strongest next experiment is to compile these hot pixel loops into larger
units, preserving callback/mapping exits and watchdog safepoints for cycles that
lack a termination proof. Reducing handoffs attacks both large native buckets.
No speedup for that proposed change is claimed here. A broad historical
[Thumb store-continuation experiment](THUMB_STORE_RESULTS.md) was rejected after
mixed/negative results on Sky Force and Snakes; it used an older counted/TLB
configuration. That history argues for a measured experiment on this fixed scene
with other-game checks, not an unconditional removal of every boundary.

## Coverage gap versus scheduling

This is partly incomplete coverage of existing optimizations. The finite-loop
optimization already selected Asphalt's framebuffer conversion and previously
improved matched **unpaced** throughput by 23.5%; that historical gain is not a
paced FPS estimate. Other hot loops contain branches, calls and stack-held
counters outside its proof. Connected-region fusion also excludes the normal
RAM Thumb path. The sampled guest instructions inspected here perform pixel
work, rather than repeatedly checking a clock or waiting request.

This CPU time attribution does not establish the cause of the entire paced deficit.
The [Hooked On investigation](HOOKED_PACED_PROFILE.md) found a genuine pixel
hotspot, yet a separate idle-wakeup intervention recovered its FPS. Asphalt's
current build includes the resulting deadline-aware idle fix, but this capture
does not measure requested guest deadlines, timer-service lateness, runnable
latency or frame dependencies. Near-1x guest time does not rule out late events;
parked samples do not distinguish correct guest waits from excessive latency.
Nor does the dispatcher sample identify which guest region caused each handoff.

Before attributing the whole Qt gap to code generation, the discriminating next
measurement is a frame/timer timeline in this same paced scene, with a targeted
scheduling probe if it shows late wakeups. Larger guest regions remain the
strongest measured CPU-cost experiment, not a demonstrated cure for paced FPS.

## Evidence and cleanup

- [Numeric results and raw artifact paths](asphalt-paced-profile.json).
- Raw root: `/home/claude/.scratch/eka-asphalt-paced-20261011/`.
- `game.mjs`, `qt.py`, `profile.mjs`, `native-game.mjs` and `native-capture.mjs`
  retain launch/capture methods; per-run `steps.log` and `qt-steps.log` retain
  scene navigation. `collect.py` validates and reduces the results.
- `wasm-native/native-attribution.json`, `dispatch-blocks.json`, captured modules,
  native binaries and annotated disassemblies retain the attribution evidence.
  Analysis uses the repository's `native_attribution.py` and the matching
  deployed `eka2l1.wasm.map`.
- Native binary SHA-256:
  `12adc27057e844a6a75140f71a24ec4ebd702068275ef0e2770cfe8627d9d9cb`.
- Served WASM SHA-256:
  `b727df4739bd8622907eda634286d27957399f445216dc777e1b73e2c254adba`.
- WASM `san-francisco-a` and Qt `barrier-a` began while driving and are excluded
  from the settled-scene comparison. The first native diagnostic selected a
  motorcycle through Instant Play; it is retained under
  `wasm-native/rejected-instant-play` and excluded from conclusions.

Owned browsers, Qt and private audio processes were closed. The original VT was
restored and temporary Xorg credentials removed. No `/dev/uinput` permissions
were changed. The live service and game assets were left unchanged.
