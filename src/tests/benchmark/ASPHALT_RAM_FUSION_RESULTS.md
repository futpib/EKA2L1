# Asphalt 2: fusing RAM Thumb helpers and branches

Source baseline: `1c7b04a4c`, `wasm-port`, 2026-10-11.
Follow-up to [the store and display changes](ASPHALT_OVERHEAD_RESULTS.md).

## Changes

1. Extend bounded Thumb leaf inlining to RAM callers and RAM callees. The existing
   acyclic analysis, 32-expanded-instruction limit and eight-site limit remain.
   Trusted direct-memory callers can inline scalar stores as well as reads and
   arithmetic. The resolver checks the address space; emitted callee bytes become
   code-cache dependencies, so mapping/lifetime validation covers them too.
   A failed access exits at the exact original guest PC, preserving previous
   effects. Calls, cycles, LR writes and unsupported instructions reject inlining.
2. Extend the existing bounded Thumb branch fusion to RAM. An acyclic successor
   path can share the caller's register locals instead of returning for a lookup
   and reload. Each copied RAM region becomes a code-cache dependency. The same
   cycle detection, eight-edge limit and compilation-size checks apply.
   Callback stop/interrupt boundaries remain; cycles still return to dispatch.

Both changes require the existing trusted-code/direct-memory configuration.
Mutation-compatible and TLB execution retain their conservative boundaries.
There is no new launch switch, instruction accounting, guest clock or pacing
change, or game/address-specific production code.

## Attribution

A fresh current-build Chrome profile put 41.44% of the CPU worker's non-wait
sample weight in `execute_chain`. The sampled drawing helper at `0x70033c00`
is `CMP / BEQ / STRH / BX LR`; `0x70037ea4` is a 19-instruction color-arithmetic
helper. Both live in game RAM, outside the existing ROM inlining coverage.

The separate inlining-only profile no longer samples those helpers as standalone
regions, and dispatch is still 34.26% of non-wait sample weight. These profiles
identify work; they are separate diagnostic captures, not controlled timings of
individual checks. The first includes the approach to the barrier. Profiler FPS
is excluded from every performance result below.

The final fusion profile puts dispatch at **23.63%** of CPU-worker non-wait
sample weight, the framebuffer loop at `0x700019c4` at **11.17%**, the textured
pixel loop at `0x70004c6e` at **7.13%**, and audio resampling at **5.09%**.
The two inlined helpers are no longer sampled separately. Attaching the profiler
also changes observed execution speed; the cause is not isolated. These shares
locate remaining work, not exact per-instruction costs or an independent proof
of the timing improvement.

## Matched measurements

Original signed Asphalt 2 1.0.1 N95 8GB; stock Nokia 5320; Arcade / Single Race /
red Volkswagen Golf GTI Mk V / San Francisco / no steering / first outside
barrier. Screenshots verify the selected car, track and measured scene. The car
nudges along the barrier; this is a repeatable scene rather than an exact replay.

Chrome 153.0.8010.52, Intel i7-10875H, NVIDIA Vulkan hardware graphics, normal
paced count-free execution, 2 ms watchdog, trusted code mode 3, direct memory 2.
Browser output is muted; guest audio processing stays active. Linux per-thread
scheduler counters provide CPU-worker time. Presentation counts include repeated
images, and emulator translation/emission/installation remains included in FPS.
One emulator runs at a time; other owned browser renderers are suspended.
Builds, compiler tests and profilers finish before accepted timing windows.
Frequency/load evidence is retained; these short shared-host windows are not
confidence intervals or guarantees for another device.

Inlining comparison order: candidate, baseline, baseline, candidate, 25 seconds
per window. Fresh browser sessions, with no profiler attached before timing.

| # | Build | FPS, first / second | Pooled FPS | CPU-worker ms/frame |
|---|---|---:|---:|---:|
| 1 | Before this change | 13.70 / 13.73 | 13.71 | 32.14 |
| 2 | RAM helper inlining | 14.45 / 14.57 | 14.51 | 27.79 |

Inlining alone adds **5.8% FPS**, with **13.5% less CPU-worker time per frame**.

Final inlining plus branch-fusion comparison: candidate, baseline, baseline,
candidate, again 25 seconds per window without profiling. The baseline browser
is reused from the first comparison; the candidate uses a fresh browser. Before
the measured race, an extra menu press entered Instant Play. Screenshots caught
that mistake, and navigation returned to the correct car and track before any
accepted timing. This is not identical cold-start history.

| # | Build | FPS, first / second | Pooled FPS | CPU-worker ms/frame |
|---|---|---:|---:|---:|
| 1 | Before this change | 13.69 / 13.90 | 13.79 | 31.90 |
| 2 | RAM helper inlining and branch fusion | 15.22 / 16.84 | 16.03 | 21.68 |

The final change adds **16.2% FPS**, with **32.0% less CPU-worker time per
frame**. The candidate's variation is material: report both windows, not just
the faster repeat. Guest time/host time remains approximately 1.0; instruction
totals remain zero. The candidate compiled 21 additional functions in its first
window and zero in its second; total recorded translation/emission/installation
was about 40 ms and 2 ms respectively, included in elapsed time.

Fresh native Qt control: **17.88 / 17.36 FPS**, pooled **17.62 FPS**, two 25-second
windows. Same package, car/track/barrier and ordinary paced Dynarmic execution.
Accelerated Intel UHD GLX on native Xorg; private PipeWire null sink. Frames are
counted at GLX swaps, versus the browser's emulator-presentation counter. This
refresh uses the unchanged reference binary, SHA-256
`12adc27057e844a6a75140f71a24ec4ebd702068275ef0e2770cfe8627d9d9cb`.
The final browser average is **91.0% of this Qt presentation rate**, about
**9.0% below Qt**. Different graphics backends/counters, moving scenery and
separate measurement windows limit the comparison; this is not a parity claim.

## Correctness

The final full WASM compiler suite passes **186 tests, zero failures**. New tests
include **2,112** independent interpreter register/flags/memory comparisons for
RAM calls and **672** comparisons of RAM branch fusion with ordinary compiled
chains, including **84** additional independent interpreter comparisons. Coverage
includes all conditional codes,
word/halfword/byte/SP-relative stores, literal and LR reads, permission failures,
page boundaries, unaligned/endian fallback, stores before later faults, callback
stop and interrupt requests. Dependency extent at the top
of the 32-bit address space, rejected calls/cycles/LR writes, callback-bearing
paths and conservative configurations are also checked. Existing cache tests
cover dependency remapping, ASID/generation changes and invalidation.

The live HTTPS launcher now serves the final frozen build; a downloaded WASM
hash matches the measured candidate. `game-picker.ts` passed on
`https://claude-laptop.lan:8188/?game=custom`, using hardware Vulkan and an
isolated PipeWire audio sink. Sky Force and Snakes gameplay, keyboard and
simultaneous touch controls, portrait/landscape layout, game switching, manual
launch and sound toggling passed, with no page/network errors. Nonzero PCM was
consumed and the AudioContext clock advanced for both games; each reported one
underrun, so this does not claim uninterrupted or subjectively verified sound.
The regression windows showed 31.38 and 15.98 presentations/s respectively;
they are gameplay checks, not paired performance comparisons for these games.
Evidence is in `picker-ram-fusion/` under the archive below. The isolated audio
services and all owned benchmark browsers/private servers were stopped after
testing; the desktop audio configuration was untouched.

## Rejected follow-up

A separate experiment retained callback-free Thumb backedges to their own entry
inside WASM, polling the external atomic watchdog, stop and IRQ at each unproved
backedge. It passed 360 interpreter comparisons plus an external-worker infinite
loop interruption test, and the full 186-test suite. But paced results were
**14.44 / 14.74 FPS**, pooled **14.59 FPS**, **27.28 ms CPU/frame**, compared with
inlining alone at 14.51 FPS / 27.79 ms. That small incremental change did not
justify adoption from these shared-host windows. Its source and dedicated fixture
were removed; frozen builds, test logs and measurements remain in the local
archive. No runtime variant or launch switch was kept.

## Local evidence

Artifacts: `/home/claude/.scratch/eka-asphalt-opt-20261011/`.
`wasm-ram-leaves-a/` and `wasm-ram-leaves-b2/` contain the first comparison.
`qt-refresh/qt-asphalt-2/` contains the fresh Qt control. `ram-loop-summary.json`
records accepted windows and aggregate values. Aborted `wasm-ram-leaves-b/` had
an incorrectly configured private server with no game catalogue; no game ran and
it contributes no measurements.

`wasm-fusion-d/` contains the final candidate timing, screenshots and separate
`fusion-profile/` diagnostic capture. Frozen WASM SHA-256:

| # | Build | SHA-256 |
|---|---|---|
| 1 | Baseline | `e16cd5357c519dac23663e6f101231bc4606a04c497fd349df8528b19a706727` |
| 2 | RAM helpers only | `937348f979cfcdc317be61f6bb0cc7b188eee26c2a3183349614696396fc8864` |
| 3 | Final RAM fusion | `c9bbc6779e43e90eb1d90b53b97b94719d2ae7378de290d7340a02b5d7708e75` |
