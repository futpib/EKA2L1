# Asphalt 2: frame, timer and native-code attribution

Measured 2026-10-11 on `wasm-port`, source `1e1922ee9` / report commit
`de68d5711`. **Both execution overhead and a frontend scheduling difference are
present.** In the accepted diagnostic traces, WASM spent 41.56 ms per frame
inside the CPU runner, versus Qt's 17.45 ms. WASM also requested a display sleep
every frame; Qt requested none. That sleep took 7.89 ms on average.

This closes the principal timing uncertainty in [the previous profile](ASPHALT_PACED_PROFILE.md).
It does not establish a measured speedup from removing either cost: no
optimization was applied or deployed. All temporary runtime instrumentation was
removed after freezing private diagnostic builds.

## Workload and measurement boundaries

Use the same original signed Asphalt 2 1.0.1 N95 8GB package, stock Nokia 5320,
Arcade → Single Race → red Volkswagen Golf GTI Mk V → San Francisco → no steering
→ first outside barrier. Start/end screenshots confirm this scene on both
frontends. The car nudges along the barrier; this is not a deterministic replay.
Both frontends are paced: count-free WASM and normal Qt Dynarmic. Presentation
counts include repeated images. Only one emulator ran at a time.

The bounded trace records host timestamps at CPU-run entry/exit, scheduler and
idle boundaries, syscall entry/exit, timer requests/delivery, display pacing and
frame submission/presentation. Linux context-switch records independently
separate time on CPU from time off CPU. No per-guest-instruction counters were
added. Debugger inspection happened after the accepted timing windows.

The WASM analysis covers **86 complete presentation intervals, 8.045661 s**.
Qt covers **150 complete framebuffer-submission intervals, 9.229690 s**. Each is
bounded by the first/last captured frame event. The separate eight-second FPS
windows have different endpoints, so their FPS is not the reciprocal of these
mean intervals. Qt submission is upstream of GLX presentation; it must not be
described as an identical display-completion marker.

## Complete elapsed-time partition

These rows are disjoint source-defined phases of the CPU worker's wall time.
They sum to every microsecond between the selected frame boundaries. A phase
can contain host preemption or blocking; it is not necessarily CPU consumption.

| # | Worker phase, mean ms/frame | WASM | Qt |
|---|---|---:|---:|
| 1 | Guest CPU runner, including synchronous services | 41.5565 | 17.4492 |
| 2 | Explicit host idle sleep | 49.1319 | 0 |
| 3 | Scheduler, including native Qt's blocking idle wait | 0.3337 | 44.0323 |
| 4 | Clock advance / timer work after running; Qt post-run bookkeeping | 0.8123 | 0.0098 |
| 5 | Loop setup | 1.0937 | 0.0129 |
| 6 | Idle decision and loop return | 0.3674 | 0.0271 |
| 7 | Return from idle / input drain | 0.2588 | 0 |
| 8 | **Total** | **93.5542** | **61.5313** |

The observed 32.0229 ms interval difference partitions into **24.1073 ms more
inside the runner** and **7.9156 ms more outside it**. This is an exact arithmetic
partition of these captures, not a causal decomposition of the earlier 17.23
versus 10.96 FPS baseline. It also is not proof that Qt's scheduler costs 44 ms
of CPU time: that phase contains its blocking wait.

Within the runner, the main game thread accounts for 39.1554 ms/frame in WASM
and 17.0454 ms in Qt; the secondary guest audio thread accounts for 2.4011 and
0.4038 ms. Measured syscall bodies total **1.7587 ms in WASM / 0.6194 ms in Qt**,
already included above. Most runner time therefore remains in guest execution
and emulator dispatch, rather than synchronous OS service bodies. Trace entry
cost and work outside the marked syscall body are not subtracted as though free.

Linux context switches give a different, overlapping view: WASM's CPU worker
was on CPU **46.4909 ms/frame**, off CPU **47.0633 ms/frame**; Qt's dominant CPU
thread was on CPU **17.6962 ms/frame**, off CPU **43.8351 ms/frame**. This includes
kernel execution and timer/scheduler work outside `cpu->run`. Do not add these
numbers to the phase table or guest waits. Qt's Linux thread identity is inferred
from the dominant task-clock stream. Browser-to-Linux clock alignment is bounded
to ±122.25 µs for the quiet trace; native timestamps already use CLOCK_MONOTONIC.

## What the game actually waits for

Debugger register/stack snapshots and ARM callsite disassembly confirm this
simplified repeating path. The snapshots identify callers; they are not a full
symbolic unwind or a timing measurement.

```text
asynchronous timer completes -> game's active-object callback
    User::After(20000)                 // call at 0x70001ef2
    update and render game
        User::After(0)                 // call at 0x70000ac4
        convert framebuffer pixels    // loop at 0x700019c4
        submit framebuffer            // indirect call at 0x700019f8
            emulator screen::vsync
            if requested: park guest thread until display deadline
    request next timer for 5000 us     // call at 0x70002212, via CTimer
    return to active scheduler; wait for request
```

Observed service arguments were exactly 20,000, 0 and 5,000 µs, once per frame.
The 5 ms request is asynchronous; the matching completion and subsequent guest
execution establish its role between frames. The current emulator's
[64 Hz timer queue](../../emu/kernel/include/kernel/timer_deadline.h) rounds
positive deadlines to 15.625 ms boundaries. A 20 ms request can therefore take
about 31 ms here. This is the implementation both frontends used, not an
independent measurement of physical-phone timing.

| # | Request → delivery, mean ms | WASM | Qt |
|---|---|---:|---:|
| 1 | `RTimer::After(5000)` | 12.1725 | 12.6908 |
| 2 | `User::After(20000)` | 31.2292 | 31.1558 |
| 3 | `User::After(0)` | 0.0182 | 0.0701 |
| 4 | Emulator display-pacing sleep | 7.8877 | No requests |

These request lifetimes overlap the worker phases and background audio work.
They must not be added to the phase table. There are 85 fully contained display
sleep lifetimes in the WASM comparison window, and 86 requests; the endpoint
rule excludes a partially contained lifetime.

The display difference comes directly from
[`screen::vsync`](../../emu/services/src/window/screen.cpp): the deterministic
timer branch, also used by paced count-free WASM, always returns the time to the
next 60 Hz boundary. Native Qt uses the host-clock branch, which skips the delay
when the preceding frame interval exceeded the display period. All 86 WASM
requests were positive (mean 8.272 ms requested), while all 150 Qt requests were
zero. [`update_screen`](../../emu/dispatch/src/screen.cpp) applies that delay to
the guest thread on the observed framebuffer call path. Publication of the
current frame and sleeping until continuation are distinct events.

**Late delivery is small in this capture.** Mean lateness against the programmed
deadline was 98 µs for WASM's game timer and 125 µs in Qt; `User::After(20000)`
was 130 µs in both. WASM display wakeups averaged 99 µs late. Notification to
the next main-thread CPU entry averaged 18 µs in WASM (p95 30 µs, maximum 440 µs)
and 76 µs in Qt. There is no multi-millisecond runnable queue delay explaining
the missing frames in this quiet run.

The apparent 1.141 ms guest-clock lateness of WASM's zero-duration yield is
different: its deadline uses the guest time last published before that CPU run.
The actual request-to-notification interval averages only 18 µs. Calling that
a 1.1 ms host wait would be a clock-attribution error.

## All sampled native functions and the remaining unknowns

Expanded the previous production-build native capture from the selected hot
functions to **all 1,197 sampled JIT code objects**. Native bytes come from the
matching JIT load records; each object has a disassembly with instruction sample
counts. The denominator remains the same **12,508 CPU-worker samples**.

| # | Disjoint native sample category | Samples | Share |
|---|---|---:|---:|
| 1 | Compiled-region dispatcher | 5,345 | 42.73% |
| 2 | Asphalt executable's ARM translations | 5,260 | 42.05% |
| 3 | Uncached compiled-code lookup | 413 | 3.30% |
| 4 | Audio resampling and pump | 388 | 3.10% |
| 5 | Chromium native functions, now symbolized | 371 | 2.97% |
| 6 | V8 builtins and JS/WASM glue | 255 | 2.04% |
| 7 | Other C++ WASM runtime functions | 241 | 1.93% |
| 8 | ROM / other guest ARM translations | 104 | 0.83% |
| 9 | Anonymous V8 code without recorded code bytes | 63 | 0.50% |
| 10 | libc: waits, clocks and memory copy/fill | 61 | 0.49% |
| 11 | vDSO clock reads | 7 | 0.06% |

The dispatcher disassembly still attributes 15.96% of all samples to indirect
call setup/checks/call, 14.27% to fast RAM code lookup, 5.06% to post-region
stop/IRQ/watchdog checks, and 4.17% to PC normalization and ROM/RAM selection.
The hot game regions contain texture/pixel loads, transparency branches, color
helpers, pixel stores and cursor updates. The tiny helper at `0x70033c00` is a
conditional halfword store. Its generated code must also perform memory checks,
publish guest state and return. The optimized framebuffer loop is only 3.36%
of CPU samples. These observations support broader coverage of hot render loops,
not an EUser polling explanation.

Matching Chromium debug symbols resolved its entire 371-sample native bucket.
It includes performance-clock access, V8 property/API access, atomic/futex waits
and allocation work. Native entry/exit disassemblies are retained. The 61 libc
samples include 37 named-symbol samples and 24 in disassembled copy/fill
implementations whose exact private symbol names are unavailable. The vDSO
clock interpretation uses the current process's mapping from the same host boot;
the old capture did not save its bytes.

**63 samples (0.50%) remain anonymous V8 code.** Their addresses and neighboring
code allocations are recorded, but neighboring functions do not prove identity.
The original process has exited and the JIT dump did not record these bytes.
They cannot honestly be disassembled after the fact. A new capture cannot
recover those historical bytes. No percentages were silently redistributed.

Function/block attribution also does not turn sampling into exact instruction
latency: only **2,006 samples (16.04%)** have exact sparse ARM/C++ source anchors.
Sample skid, preceding stalls and missing V8 arithmetic positions remain.
The complete elapsed-time partition is exact for the recorded boundaries;
the native cost distribution is a statistical profile from a separate run.
Multiplying its percentages by the new frame time would create false precision.

The region handoff itself is WASM-to-WASM. Genuine JS crossings are visible in
clock imports and runtime glue; they are not the 42.73% dispatcher bucket.

## Interpretation and limits

There are two independently evidenced targets: the browser's extra display wait,
and repeated handoffs in render loops that existing finite-loop/fusion policies
do not cover. The first is a scheduling-policy difference to test against the
same benchmark. The second is the dominant measured CPU-cost opportunity.
Removing the display wait cannot be assumed to subtract exactly 7.89 ms from
every frame: it changes phase relative to the 64 Hz guest timer queue. Neither
an optimization nor a resulting FPS gain was tested in this investigation.

The accepted short trace windows measured 10.79 FPS WASM and 15.50 FPS Qt;
nearby trace-disabled controls measured 10.54 and 16.50 FPS. These are
diagnostic observations, not replacements for the earlier uninstrumented
30-second comparison, and one adjacent control does not calibrate overhead.
WASM compiled zero new functions in the accepted window (0.783 ms recorded
translation activity), so emulator compilation does not explain it.

An earlier capture during external build contention measured 4.01 FPS and
124.07 ms on CPU per frame. It is retained as host-contaminated evidence,
excluded from the main comparison. A prior 20-second capture overflowed the
trace buffer and then failed in the exporter; its usable prefix and failed
follow-up are retained, explicitly excluded here. Accepted traces have no
overflow, perf losses/errors, or unsampled new/missing threads.

## Reproduction and evidence

- [Trace reducer, event definitions and diagnostic patches](asphalt-timeline/README.md).
- [WASM reduction](asphalt-timeline/wasm.json), [Qt reduction](asphalt-timeline/qt.json),
  [all sampled native functions and external symbols](asphalt-timeline/native.json).
- [ARM callsites](asphalt-timeline/guest-callers.asm) and
  [sampled native dispatcher](asphalt-timeline/dispatcher.sampled.asm).
- [Artifact paths, hashes and retained failures](asphalt-timeline/manifest.json).
- Raw root: `/home/claude/.scratch/eka-asphalt-timeline-20261011/`.
  `expanded-native/` contains every captured JIT function's bytes/disassembly;
  `external-native/` contains symbolized Chromium/libc and vDSO disassembly.

Private browser, Qt, server and audio processes were closed; debugger
breakpoints removed; original VT restored and temporary Xorg credentials
removed. Runtime source and the original native Qt binary were restored.
The live `.lan` service, ROM and game packages were left unchanged.
