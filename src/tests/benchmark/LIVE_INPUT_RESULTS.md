# Live browser input and virtual-clock pacing

The normal Start flow now enables the verified AOT region backend and the same
virtual guest clock used for deterministic replay. Host time only limits running
ahead; it never advances the guest clock or changes guest instruction budgets.
Keyboard and pointer press/release transitions enter a queue consumed by the
emulation thread. Blur/visibility changes release held controls. Keyboard and
touch sources sharing a key are reference-counted by source.

Live mode skips diagnostic framebuffer readback. Audio consumption and callbacks
still use guest time, but PCM/event export buffers are not retained and the
benchmark's 120-second capture limit does not apply. Speaker output remains off;
audio-quality work is deferred. A native regression runs audio beyond 120 guest
seconds and checks consumption plus zero retained PCM frames.

## Measured live workflow (before further CPU optimization)

`node live.ts ASSETS NEW_OUTPUT 60` used the actual file inputs, Start button,
menu keyboard input, arrow controls, touch controls and shutdown on Chromium
150 / the physical NVIDIA GPU. Desktop viewport was 900x760; the narrow touch
layout was 390x844, with no horizontal overflow. This is responsive-layout and
emulated-touch coverage, not a physical phone test.

The 60.092134-second observation advanced from 23.375194 to 74.665080 guest
seconds: **0.853521x realtime**. This does not meet the realtime goal. Gameplay
remained visible at both endpoints. Twelve DOM keydown-to-guest-queue-consumption
measurements ranged 0.625–9.378 ms; these exclude display response latency.

The checked 85-image benchmark still matches native exactly across pixels,
presentation ordinals, timestamps, instruction counts, PCM and audio events.
All three native CTest targets pass, including the new live-audio test.
The CPU compiler was unchanged in this stage. Full replay and further sustained
play checks continue with the next performance stage.

Raw artifacts: `/home/claude/.scratch/eka-benchmark/live-ui-first` (report, GPU
information, screenshots and browser log), `live-benchmark-smoke` (exact
comparison), and `live-base-build` (archived binary/hash/source patch).

Run the page through `src/tests/wasm/serve.ts`, choose the ROM/RPKG/SIS files,
enter the app caption, and click Start. Arrows/WASD move, Enter/Space select,
and F1/F2 access softkeys; on-screen controls support held touches.
