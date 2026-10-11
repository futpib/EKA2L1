# Asphalt 2: reducing region and display overhead

Source baseline: `d4823b736`, `wasm-port`, 2026-10-11.
Store-continuation implementation: `dc0fc457e`. These changes act on
[the measured dispatcher and display costs](ASPHALT_FRAME_TIMELINE.md).

## Changes

1. Continue through ordinary Thumb RAM stores when using trusted direct memory.
   A successful scalar access cannot remap memory or request a guest interrupt.
   Keeping the following instructions in the same function avoids publishing
   registers, returning to the dispatcher, looking up the successor, calling it,
   and reloading state after every store. A failed scalar access returns before
   that guest instruction, with earlier side effects preserved; the interpreter
   performs the access. Multi-register callbacks retain the conservative exit.
   Mutation-compatible code and the TLB path retain their previous policy.
   Branch exits, finite-loop proofs and watchdog behavior are unchanged.
2. Use Qt's existing missed-refresh policy with the virtual clock as well.
   Paced count-free WASM uses this clock. Previously it always requested another
   wait to the next display phase, even after a slow frame. Now slow frames skip
   the wait, while early frames remain paced. This is a scheduling correction,
   distinct from the CPU saving above. The guest clock, game timer requests and
   instruction-accounting policy are unchanged.

## Method

Original signed Asphalt 2 1.0.1 N95 8GB, stock Nokia 5320, ordinary paced Chrome
153.0.8010.52, Intel i7-10875H. Arcade -> Single Race -> red Volkswagen Golf GTI
Mk V -> San Francisco -> no steering -> first outside barrier. Start/end
screenshots are reviewed; the car nudges along the barrier and this is not an
exact replay. FPS counts emulator presentations. Browser sound is muted but
emulated audio still runs. The guest clock remains approximately 1x host time.

Only one emulator runs during accepted measurement windows. Other owned browser
renderers are suspended. Builds and compiler tests finish before timing.
CPU-worker time comes from Linux per-thread scheduler counters, independently
of Chrome's sampling profiler. CPU frequency and host load are retained.
Profiler windows are excluded from performance results. No new production
instrumentation or experiment switch is added.

## Initial store-continuation comparison

Order: candidate, baseline, baseline, candidate; 30 seconds per window.

| # | Build | FPS, first / second | CPU-worker ms/frame, pooled |
|---|---|---:|---:|
| 1 | Baseline | 10.98 / 11.62 | 39.95 |
| 2 | Store continuation | 11.95 / 11.96 | 35.52 |

Pooled FPS: **11.30 -> 11.95 (+5.8%)**. CPU-worker time per frame falls
**11.1%**. This shared host changes frequency/load; these are observed windows,
not a confidence interval or a promise of the same gain on another device.

## Three-build confirmation

Fresh store-only and combined sessions; the original baseline session is reused.
No profiler has been attached to any of these sessions. Order: stores, combined,
baseline, combined, stores, baseline; 25 seconds per accepted window.

| # | Build | FPS, first / second | Pooled FPS | CPU-worker ms/frame, pooled |
|---|---|---:|---:|---:|
| 1 | Original baseline | 10.45 / 10.48 | 10.46 | 47.13 |
| 2 | Store continuation | 11.62 / 11.76 | 11.69 | 36.85 |
| 3 | Stores + missed-refresh correction | 13.59 / 13.58 | 13.58 | 33.56 |

Combined improvement: **29.8% more presentations per second** in this round.
The display change adds **16.2% FPS** over store continuation alone. CPU-worker
ms/frame includes fixed-rate audio/runtime work; do not attribute the entire
33.56 versus 36.85 difference to cheaper rendering instructions. Nor should
these short windows be treated as an exact causal partition of the FPS gain.

All accepted windows have zero browser/emulator errors and guest/host clock
ratios within 0.005% of 1. Translation, emission and installation remain
included in FPS; their largest combined total is 42.847 ms in a 25.21 s window.
These results do not establish Qt parity: the old Qt measurement was a separate
capture and was not rerun here.

## Remaining cost

A separate store-only Chrome profile still attributes about 42.8% of non-wait
CPU-worker sample weight to `execute_chain`, 46.1% to generated guest code, and
2.3% to uncached region lookup. The small conditional pixel-store helper at
`0x70033c00` and the texture loops remain visible. Store continuation removes a
subset of handoffs; calls and unproved loop edges still dispatch. This work does
not claim all dispatch overhead is gone.

The profiled run was faster than the preceding ordinary window, so its FPS and
CPU/frame are excluded from every speedup table. The cause of that difference
was not isolated. The final confirmation uses fresh, unprofiled sessions.

## Correctness

- Full WASM compiler suite: **184 passed, 0 failed**.
- Store-continuation fixture expanded to **432** independent interpreter
  register/flag/memory comparisons. It covers word/halfword/byte and signed
  loads, register/SP-relative addresses, conditional exits, denied permissions,
  unmapped successor pages, alignment, endian fallback, and successful stores
  preceding a later miss. It verifies the mutation-compatible store boundary.
- Native timer/display tests: **72 assertions in 8 cases**, including slow-frame
  wait removal followed by early-frame pacing and recovery after another slow
  frame.

## Browser validation and deployment

The initial private launcher run passed rendering, keyboard/touch, switching and
layout checks for Sky Force and Snakes, but failed its audio assertions. The host
AudioContext remained at time zero even for a standalone oscillator with no
emulator loaded. A null sink on that same host audio service also stalled.

A separate PipeWire/Pulse server and policy instance, selected only for the test
browser, restored the standalone audio clock (3.008 seconds in a three-second
probe). This tests real browser worklet consumption of nonzero PCM; physical
speaker output is not claimed. The host desktop audio services were not restarted.
The full launcher check **passed** against `https://claude-laptop.lan:8188/`
with this isolated audio route and hardware NVIDIA Quadro T1000 Vulkan rendering.
Both games passed keyboard/touch, nonzero worklet PCM, sound toggling, switching,
mobile/landscape layout and manual-launcher checks, with no browser errors.
Screenshots confirm Sky Force combat and Snakes gameplay. Each audio window
reported one underrun and zero dropped frames/resyncs; this is not a claim of
perfect glitch-free sound. Final evidence is in `picker-lan/report.json`;
`picker-candidate/report.json` preserves the initial host-audio failure.

Snakes/Sky Force are compatibility checks, not matched speedup measurements for
those games. Other catalogue games and all levels are not covered by this run.
The `.lan` served WASM hash matches the frozen combined build below.

## Evidence

Local raw artifacts: `/home/claude/.scratch/eka-asphalt-opt-20261011/`.
`baseline/`, `stores/`, and `vsync/` are frozen build directories.
`wasm-base/` and `wasm-stores/` hold the initial accepted comparison.
`wasm-stores-fresh/` and `wasm-vsync-2/` hold subsequent fresh-session checks.

The first `wasm-vsync-2/vsync-a` window is **rejected**: navigation entered
Instant Play with a motorcycle. Its files remain for audit. The corrected
Golf navigation and screenshots are recorded separately. The failed initial
`wasm-vsync/` launch was a private-server startup race, before game execution.

Frozen WASM SHA-256:

| # | Build | SHA-256 |
|---|---|---|
| 1 | Baseline | `b727df4739bd8622907eda634286d27957399f445216dc777e1b73e2c254adba` |
| 2 | Stores | `a986ffb0c9995b286685ffd48d08b89b9f21723434efe44d21066f9634cde4a3` |
| 3 | Stores + display | `e16cd5357c519dac23663e6f101231bc4606a04c497fd349df8528b19a706727` |
