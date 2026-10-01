# Qt throughput and headroom

Measured 2026-09-27 on the same i7-10875H / Quadro T1000 Max-Q desktop.
**Qt's normal desktop CPU backend, Dynarmic JIT, reaches about 4.33x realtime
on the heavy replay when unpaced.** It needs about 23% of the real-time wall
budget, leaving about 77% unused in this controlled window. This is substantially
more margin than the browser's recent 1.032x result.

| Path | Host seconds for 18 guest seconds | Mean throughput |
| --- | --- | --- |
| Qt / Dynarmic JIT | 4.14096, 4.16640 | **4.3335x realtime** |
| Qt / DynCom interpreter | 29.9878, 29.0294 | **0.6100x** |
| Browser / compiled WASM, prior batch | 17.6534, 17.2201 | **1.0323x** |

The JIT has about 333% additional throughput over the real-time requirement in
this test. It is about 4.2x faster than the browser's recent measured heavy-window
throughput. The browser figures are from the earlier batch in HEADROOM_RESULTS.md,
not a newly interleaved browser control. Do not generalize this margin to all games,
scenes, hardware or a sustained-session minimum.

## What was measured

Four fresh device states run serially in interpreter/JIT/JIT/interpreter order.
The window is 78–96 guest seconds, with the same Snakes assets and scripted input.
Rendering stays enabled on accelerated NVIDIA GLX (PRIME offload); swap interval
and driver vsync are disabled. No PNG/readback capture, profiler sampling or audio
export retention is active. Native builds/tests finish before timing starts.
Qt uses native OpenGL; the previous browser batch uses NVIDIA Vulkan through ANGLE.
The native build is the existing configured build, not a new compiler-flag tuning
experiment. Raw binary identity, GLX renderer details and source state are in
`QT_HEADROOM_EVIDENCE.json`.

This uses the common virtual guest clock to measure unpaced capacity. Normal Qt
playback is paced; this is not a claim that normal gameplay runs 4.3 times fast.
The native JIT normally uses different scheduling quanta outside benchmark mode,
so this is a controlled-backend headroom estimate, not a complete benchmark of
every normal Qt configuration.

**The old native correctness reference forces DynCom.** Its speed must not be
presented as normal Qt JIT speed. The new JIT override is explicit and native-only;
the default exact-reference benchmark still selects DynCom.

The interpreter's measured window exactly matches the browser work counters:
3,975,200,506 instructions and 676 presentations, from 78,000,000 to 96,000,000 us.
Both JIT runs reproduce their own counters: 3,981,611,134 instructions and 676
presentations, ending at 96,000,008 us. That is about **0.161% more instructions**,
consistent with different JIT budget exits/scheduling. This is a close workload
comparison, **not pixel/state-exact JIT parity**. No new JIT framebuffer comparison
was made. Sound quality remains outside this test.

## Instrumentation and reproduction

The native frontend accepts `EKA2L1_QT_PROFILE_OUTPUT` only with benchmark mode.
It measures the fixed 78–96s window and writes the common performance report.
`EKA2L1_QT_PROFILE_CPU=dynarmic` selects the JIT for this explicit experiment;
otherwise the benchmark uses DynCom. Flags are configured before threads start.
All three native CTest targets pass after the instrumentation changes. The WASM
frontend/binary was not rebuilt or changed for this measurement.

```sh
# Use your accelerated display and its matching X authority.
DISPLAY=:1 XAUTHORITY=/path/to/xauthority \
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
python3 src/tests/benchmark/run_qt_profile.py \
  --assets ASSETS --template NATIVE_REFERENCE/template --output NEW_OUTPUT
```

The template is the fresh device-only state made by `run_native.py`; each trial
installs the same SIS into a copy. The runner records source/binary/input identity,
GLX details and every measurement. It requires an accelerated display; Xvfb's
software renderer would be a different comparison. Raw logs and state snapshots
are retained at `/home/claude/.scratch/eka-benchmark/qt-headroom/`.
