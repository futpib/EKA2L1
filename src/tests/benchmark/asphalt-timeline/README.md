# Asphalt frame-timeline evidence

These are archived diagnostic tools and results for
[ASPHALT_FRAME_TIMELINE.md](../ASPHALT_FRAME_TIMELINE.md). None of the runtime
instrumentation is active in normal builds.

## Reproduce the reduction

Raw files are retained at `/home/claude/.scratch/eka-asphalt-timeline-20261011`.
From the repository root:

```sh
python src/tests/benchmark/asphalt-timeline/reduce.py \
  /home/claude/.scratch/eka-asphalt-timeline-20261011/wasm-trace-clean/barrier-trace-quiet.trace
python src/tests/benchmark/asphalt-timeline/reduce.py \
  /home/claude/.scratch/eka-asphalt-timeline-20261011/qt-asphalt-2/barrier-trace-a.trace
```

The outputs reproduce `wasm.json` and `qt.json`. The reducer requires the
matching `-trace.json` and `-switches.json` sidecars. It rejects overflow,
missing/new sampled threads, perf errors/losses, incomplete phase coverage and
unmatched syscall entries within the comparison window. Equal timestamps retain
recording order: sorting whole row tuples would falsely put CPU entry before
the notification at the same timestamp and invent a long runnable delay.

`manifest.json` records raw evidence hashes and excluded attempts. `native.json`
is an inventory of every sampled JIT code object, disassembly path, byte hash,
sample count and symbolized external address. It uses the earlier production
native profile; it is not a profile of the new instrumented frame window.

## Repeat a capture

Use an isolated source snapshot of `1e1922ee9`, with the same assets and build
configuration as the previous profile. The patch variants intentionally match
the two diagnostic builds that were actually run. For WASM, copy `trace-wasm.h`
to `src/emu/common/include/common/asphalt_timeline_trace.h` and apply
`instrumentation-wasm.patch`. For Qt use `trace-qt.h` and
`instrumentation-qt.patch`. The patch includes were normalized from the scratch
header's absolute path to that include path; the event sites are unchanged.
Build/freeze the artifact, then restore source before doing unrelated work.
Do not apply both patches or deploy these tracing builds as an optimization.

The retained `game.mjs`, `qt.py`, `debug-guest.mjs`, per-gameplay `steps.log`,
`qt-steps.log` and build logs in the raw root record actual launch/navigation
commands. They refer to this host's installed browser, native display, audio
setup, fixture directories and earlier benchmark helpers. `capture.mjs` accepts
the browser harness's `{page, browser, out, measure}` and an eight-second label;
`qt_trace.py` accepts the native process and measurement callback. The archived
browser helper removes a redundant sampler-stop call and uses relative tool
imports; neither changes the recorded event format. The raw root retains the
exact scripts used for the captures.

`sample_switches.py` records Linux task-clock samples and context switches,
using stdin READY/stop synchronization. It requires Linux x86-64 and the same
perf permissions as `sample_native.py`. Stop recording before exporting shared
memory. WASM extraction must use the global `HEAPU8`, not `Module.HEAPU8`, and
copy shared bytes before `TextDecoder` reads event names. The first failed
export exposed both requirements; that attempt is excluded from the accepted
comparison. Browser capacity is 262,144 rows: eight seconds fitted this scene,
but always check the returned count rather than assuming it cannot overflow.

The private service used port 8194 and a frozen diagnostic build. Production
`.lan` on 8188 remained on its existing build. No ROM/SIS files are in this
evidence directory.

## Event format

Each row is eight little-endian uint64 values:
`host_us, guest_us, kind, pthread_self, a, b, c, d` (64 bytes).
Browser host time follows Emscripten's steady-clock implementation and is aligned
to Linux CLOCK_MONOTONIC using 20 bracketed page-clock reads. Native host time is
already CLOCK_MONOTONIC. Only native event kinds 9/11 have meaningful guest time;
other native rows use the inactive benchmark clock and must not be interpreted
as guest time zero. Kinds 1–7 partition the CPU worker's wall time.

| # | Kind | Meaning and payload |
|---|---|---|
| 1 | 1 | Loop selected guest thread: UID, PC, LR, SP |
| 2 | 2 / 3 | Before / after CPU runner: UID, PC, LR, SP |
| 3 | 4 / 5 | Before / after scheduler; kind 5 includes selected UID |
| 4 | 6 / 7 | Before / after host idle sleep; kind 6 includes requested µs |
| 5 | 8 | Guest-clock advance: elapsed µs, host origin |
| 6 | 9 / 10 | Timer callback begin / end: type, userdata, deadline; kind 9 has name pointer |
| 7 | 11 | Timer scheduled: type, userdata, deadline, name pointer |
| 8 | 12 | Display sleep: guest UID, duration, PC, LR |
| 9 | 13 | Guest sleep-until: guest UID, deadline, PC, LR |
| 10 | 14 | Sleep notification: guest UID, error |
| 11 | 15 / 16 | Guest request-semaphore wait / signal |
| 12 | 17 / 18 | Kernel timer request / completion: requester UID, timer UID, deadline / defer count |
| 13 | 19 / 20 / 21 | SVC entry (number, PC, LR, SP), arguments (R0–R3), return (number) |
| 14 | 22 | Display pacing: requested sleep, refresh rate, LR |
| 15 | 23 | Framebuffer submitted |
| 16 | 24 | Browser presentation callback: presentation count |

Guest main-thread UID 128 is fixed by these captured fixtures; the reducer's
notification-to-run calculation uses it explicitly. Qt's scheduler marker spans
the blocking idle wait. Request-to-delivery and on/off-CPU views overlap the
exclusive source-phase partition and must not be added to it.

The guest snapshots contain live ARM registers and a stack window, captured
after the timing runs. The CPU state address is obtained from the paused
interpreter's locals and verified by PC/LR/SP before translating the stack
through its published memory view. Stack words are not automatically a full
call chain; `guest-callers.asm` verifies the relevant return addresses.
