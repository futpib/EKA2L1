# Hooked On: paced WASM deficit and idle-wakeup experiment

Measured 2026-10-11. The WASM idle-sleep path is a demonstrated contributor to Hooked On's paced FPS deficit. Reducing only its host sleep from 1 ms to 0.1 ms in a private diagnostic binary recovered 31.71–31.76 FPS, versus 28.53–29.01 in fresh default clear-map windows. Guest time stayed at 1x. The deployed build was not changed.

The previous [ten-game comparison](QT_VS_WASM_PACED_FPS.md) reported Qt 31.66 and WASM 26.00 FPS. That campaign included introductory help transitions; it is not a matched-scene estimate of this experiment's gain. The fresh diagnostic reaches approximately the earlier Qt rate, but Qt was not remeasured here.

## Timing evidence

All windows below are unprofiled, normal paced, count-free Chrome runs on the same laptop. The scene is the Costa Rica map with the boat stationary and the water/clouds animated, after completing the anchor introduction and returning from fishing so help no longer reappears. FPS counts screen presentations, including repeated images, per host second.

| # | Configuration | FPS | Frames / host seconds | Guest clock / host clock |
|---|---|---:|---:|---:|
| 1 | Default: 1 ms idle sleep, 2 ms watchdog, A | 29.01 | 590 / 20.34114 | 1.00003 |
| 2 | 1 ms idle sleep, 0.5 ms watchdog | 29.31 | 594 / 20.26880 | 1.00002 |
| 3 | Restored default, B | 28.53 | 580 / 20.33259 | 0.99997 |
| 4 | Private 0.1 ms idle sleep, default watchdog, A | 31.76 | 641 / 20.18275 | 1.00000 |
| 5 | Private 0.1 ms idle sleep, default watchdog, B | 31.71 | 640 / 20.18525 | 1.00000 |

The watchdog probe did not recover the gap. The idle-sleep probe did, without changing the guest program, timer intervals, clock speed, compiler, or generated guest code. These are short windows on a shared host, with the idle variant in a separate fresh browser launch rather than randomized ABBA runs. The evidence supports idle wakeup/dispatch latency as a cause; exact deadline lateness was not traced.

## What the profile actually spends time doing

Chrome CDP sampled the main page, CPU worker and graphics worker at 1 ms intervals. Linux per-thread scheduler counters independently measured actual CPU consumption, because a profiler sample in `emscripten_futex_wait` represents parked elapsed time rather than CPU work.

| # | Default-build scene | CPU worker parked samples | Actual CPU worker time | Graphics worker parked samples | Generated code / active CPU-worker samples |
|---|---|---:|---:|---:|---:|
| 1 | Clear stationary map | 81.82% | 3.93 s / 20.35 s, 19.3% of one core | 94.33% | 69.63% |
| 2 | Moving boat, Up held | 78.71% | 4.35 s / 20.32 s, 21.4% of one core | 93.08% | 70.01% |

This is substantial CPU headroom. Sleeping is expected guest behavior, not 80% removable overhead; low average utilization also does not exclude expensive work on the frame's critical path. The graphics worker was mostly waiting, not continuously consuming a CPU core.

The largest active hotspot is generated region `r_57603_pc_1961531664`, entry `0x74ea9910`, in `COTD.exe` at module offset `0xa9910`. It accounts for **31.19% of active samples stationary and 34.61% moving**. ARM disassembly shows run/skip decoding, 16-bit pixel copying, and color-channel accumulation/clamping: game-side software rendering. This interpretation is inferred from instructions, not recovered source symbols.

`execute_chain` contributes another 7.89–8.42% of active samples; `_emscripten_get_now` contributes 5.44–5.60%. The hot region's emitted WASM contains direct-arena bounds tests/address adjustments for scalar pixel accesses, with page-table and helper fallback paths. Whole-region samples and static instruction counts cannot establish which of those checks or fallbacks are expensive. This is a concrete throughput target for later native-path attribution, separate from the proven idle-sleep effect.

The clear-map profile compiled no functions. The moving-map profile compiled 79 functions, totaling only 18.186 ms of translation, emission and installation across 20.21 seconds. Startup compilation does not explain these steady-state results.

## Guest timing and the frontend difference

Debugger inspection was performed only after the accepted production timing/profile windows; breakpoint timing is not benchmark evidence.

- `COTD.exe +0xdf0` divides a value inferred to be the clock frequency by 32. The loop compares elapsed ticks against that threshold at `+0xe64` and resets the previous timestamp at `+0xe78`: a nominal 32 Hz update gate inferred from disassembly.
- The COTD **Main** thread repeatedly requests `kernel::timer::after_high_res` with **30,000 us**. Its guest stack contains `ngibackbuffer.dll +0x5644` and `COTD.exe +0xf30`. The N-Gage backbuffer callback rearms the timer and signals a waiting request. It is asynchronous; this does not prove a simple serialized “render, then sleep 30 ms” loop.
- [high_res_timer_deadline](../../emu/kernel/include/kernel/timer_deadline.h) schedules that request 30–31 ms after the current guest timestamp, reflecting 1 ms nanokernel ticks.
- In WASM, [system_impl::loop](../../emu/system/src/epoc.cpp) advances paced guest time at watchdog boundaries or while idle, then calls `sleep_for(milliseconds(1))` whenever no guest thread is runnable. Timer service resumes when the emulator loop wakes.
- The native [timer loop](../../emu/kernel/src/timing.cpp) runs on a separate thread and waits using the next scheduled event's deadline.

Thus the game has a tight frame-pacing threshold and WASM adds polling/wakeup latency around timer-driven work. Shortening that specific sleep recovered the expected rate. The profiling and intervention do not separate every source of overshoot, nor establish that the pixel loop is as fast as Qt's translation.

## Diagnostic scope, cost and next target

The private test intercepted only `/eka2l1.wasm` in its own browser. At binary instruction offset 823971 in function 480, it changed the `i64.const 1000000` nanosecond argument immediately before `std::this_thread::sleep_for` to `100000`. Exactly three bytes differ, at offsets 823972–823974; other time-conversion constants are unchanged. Module length is unchanged and `WebAssembly.validate` passes. This avoids a compiler/build difference between variants. No source or deployment patch was made.

Both diagnostic gameplay windows passed with no recorded page/guest access faults and less than 0.5 ms of emulator compilation each. Their busiest renderer thread consumed 24.51% and 23.95% of one core. These CPU-only samples did not independently map Linux thread IDs to guest worker identities; their difference from profiled production CPU times is not a controlled CPU-cost comparison.

The next production target is **the paced idle wait and timer wakeup path**, with deadline-aware waiting and CPU-cost validation. The 0.1 ms constant is a diagnostic, not an adopted default or a universal recommendation. The largest remaining active-code target is the identified COTD software-rendering region.

## Reproduction and retained evidence

- Hardware: Intel Core i7-10875H; Chromium 153.0.8010.52, hardware ANGLE/Vulkan on NVIDIA Quadro T1000 Max-Q. Browser audio output muted. One EKA2L1 instance sampled at a time.
- Stock Nokia 5320 RM-409; Hooked On 0.74 BiNPDA container from the existing launcher manifest. This is not a new package or full-playthrough verification.
- Checkout `5c4bb64b4` on `wasm-port`; deployed executable source `86a64bdea`.
- Production WASM SHA-256: `dec37577f64f2b6addc7d3d1d9e877fe4bcd288bbc0b39022fd4c22236130f63`.
- Private diagnostic SHA-256: `e4bd0e9bc60141f5a057de270a7946dcfac8e830636e7759b0b7137da7382742`.
- Numeric results, frame counts, CPU shares, artifact paths and limitations: [hooked-paced-profile.json](hooked-paced-profile.json).
- Raw profiles, screenshots, guest stack/register captures, disassembly and generated WASM: `/home/claude/.scratch/eka-qt-wasm-ten-20261011/wasm-hooked-profile/`.
- Private successful diagnostic: `/home/claude/.scratch/eka-qt-wasm-ten-20261011/wasm-hooked-idle100-fresh/`.
- Harnesses in the parent scratch directory: `wasm.mjs`, `idle-fresh.mjs`, `profile-helpers.mjs`. They use the repository's `chrome-profiler.ts` and `cpu-time.ts`; per-run `steps.log` records input and capture commands.

An earlier map capture was excluded because the anchor help returned. A restored-files shortcut for the idle diagnostic exited COTD with `-1000008` and produced no gameplay timing; the successful test used ordinary fresh installation. Neither failed run contributes to the results above.

Owned test browsers were closed. The live LAN WASM was downloaded again and its production hash verified unchanged.
