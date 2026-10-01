# Browser log and GL diagnostic overhead

**Implemented and verified.** Full-capture throughput improves **1.50x**. The
paired no-capture controls show only a nominal **1.027x** with overlapping
ranges, so they do **not establish a meaningful no-capture speedup**. The new
page profile confirms that log rendering and per-call GL error polling no longer
dominate. This is not realtime gameplay.

Raw evidence, hashes and artifact paths:
[GRAPHICS_DIAGNOSTICS_EVIDENCE.json](GRAPHICS_DIAGNOSTICS_EVIDENCE.json).

Runtime source: `bff04fec45f95c7203b79aa02a58dc64e7333253`.
Baseline: `36a6a6429ed3949648e498d77be55e051c5ae59f`.
The original branch is **wasm-port**, tracking `origin/wasm-port`. All previous
benchmark, audio and compiler work was retained by fast-forward ancestry; no
rebasing, squashing or remote push was performed. The primary checkout now uses
that original branch.

## Changes and boundaries

The browser log is a native expandable `details` panel, initially collapsed.
Console output remains complete; only the optional DOM history is truncated.
It holds at most 200 lines and 32,768 UTF-16 code units, plus a truncation marker.
Closed panels perform no text/scroll updates. Visible updates coalesce on the
next animation frame. Error messages are included in the bounded history and
still go to the console. HTML-looking messages render as text.

WASM no longer polls `glGetError` after every GLAD call by default. Native
platforms retain their prior diagnostic behavior. Explicit error/status queries
used for functional fallbacks and shader/program validation remain in place.
Use `EKA2L1_GL_DIAGNOSTICS=1` with the browser runners to restore per-call checks.
For manual browser setup, call
`Module.ccall('eka2l1_graphics_diagnostics_configure', 'number', ['number'], [1])`
before `eka2l1_init`; configuration is rejected after initialization. The API
accepts only zero or one. It does not change any guest clock or CPU semantics.

Runner metadata records both the effective diagnostic state and whether the
build exposes its configuration API. This permits comparison with the archived
baseline, which always has per-call checks enabled. No Emscripten assertion,
stack-check or outer abort-handler build flags changed.

## Full-capture measurements and profile

AOT mode 4, identical 21–25 guest-second window, full capture, SwiftShader.
Correctness and unit/frontend checks finished before the isolated CPU profile;
serial timings then ran old/new/new/old with sampling and verification disabled.

| Build | Host seconds (two trials) | Mean |
| --- | --- | ---: |
| Baseline | 23.4085, 25.0098 | **24.20915** |
| Bounded/collapsed log, GL diagnostics off | 16.1719, 16.0352 | **16.10355** |

Observed full-capture throughput improves **1.5033x**, reaching **0.24839x
realtime** for this workload. Shared-host load and two trials per binary limit
precision; these are observed results, not universal speed guarantees.

The sampled run took 23.5246 host seconds, with a 23.755188-second emulation-worker
sample span; profiling overhead makes it unsuitable as the elapsed-time headline.
Page `_flushLog` self-time is **0.007411 seconds**, versus 6.650395 seconds in the
previous saved profile. No `getError` samples were recorded in the measured page
or emulation-worker profiles; explicit functional error queries still exist.
The page is **95.4% idle** in this profile. Worker graphics-wait instrumentation
records **1.81646 seconds**, versus 8.90511 seconds in the prior sampled run.
Those prior profiles identify the old costs; use the new paired timing trials,
not subtraction of overlapping profile scopes, for the speed comparison.

The worker's remaining sampled self-time is led by validated RAM lookup
(**20.20%**), the interpreter loop including shared AOT dispatch (**9.93%**),
compiled lookup (**9.73%**) and mapping resolution (**7.83%**). Combined lookup
and mapping is about **37.76%**. The page is no longer the dominant sampled CPU
consumer. Address-space/code validation and compiled execution remain the next
CPU-side targets; this change does not implement new mapping generations or
larger compiled regions.

## No-capture control and limits

Fresh fixtures ran the same workload with normal rendering but no framebuffer
readback/PNG output. Measurements ran serially old/new/new/old after warmup.

| Build | Host seconds (two trials) | Mean |
| --- | --- | ---: |
| Baseline | 18.0190, 21.3655 | **19.69225** |
| New | 20.0211, 18.3297 | **19.1754** |

The mean ratio is **1.02695x**, much smaller than the variation within these
pairs. The ranges overlap: no meaningful no-capture speedup is established.
The measured new mean is **0.20860 guest seconds per host second**, still about
**4.79x** short of realtime in this batch. Do not combine this mean with the
previous session's baseline to claim a regression or speedup; the matched
controls show substantial shared-host/timing variation. More trials or a
controlled host would be needed to quantify a small no-capture gain reliably.
Full-capture and no-capture are separate batches: their absolute means must not
be used to infer that enabling capture makes execution faster.
The full-capture improvement primarily removes measurement/presentation-side
overhead; it must not be advertised as a 1.50x interactive-emulation gain.

Both full-capture and no-capture trials retain the exact guest window, instruction
counts and 170 presentations. All captured 85-frame timing/profile windows match
native pixels/guest records. No-capture trials have no pixel evidence by design;
the full repeated replay is the image/PCM correctness gate.

## Correctness and tests

The full WASM suite passes **125 tests**, including the existing explicit
expected crash-reproduction harness limitation. Native CPU tests pass; the
broader native package has **84 passing cases and seven previously observed
failures**. No clean-base run establishes those failures' origin. The frontend's
full default `npm test` suite passes all **seven browser smoke tests**, including
initialization/shutdown; tests use Chromium via `PUPPETEER_EXECUTABLE_PATH`.
The legacy external-download end-to-end script was not rerun; the hash-checked
asset replays below supply the integration/gameplay verification instead.

Both full browser repeats match native exactly across **1,000 distinct gameplay
images**, every guest timestamp/instruction count, PCM samples and audio-event
records, ending at **68.040042 guest seconds / 10,342,580,529 instructions**.
The window spans 47.013860 gameplay seconds. One repeat also checked every
1,024th compiled block against the interpreter without divergence; this is
sampled, not exhaustive, block verification.

The diagnostics-on correctness control matches all **85 native gameplay images**
and guest timestamps/instruction counts for 21–25 guest seconds. It ran alongside
other checks, so its elapsed time is not used as a speed comparison.

## Verification and reproduction

`node log-ui.ts` from `src/tests/wasm` runs actual Chromium DOM tests at 1280px
and 390px using the shell without booting the emulator. It checks collapsed DOM
stability, bounded history after 5,000 messages, reopening, stderr visibility,
100,000-character input, console retention and HTML text safety. Gameplay
replays independently exercise the compiled frontend and rendering path.

Use [README.md](README.md) for builds/assets. From `src/tests/wasm`:

```sh
EKA2L1_BENCHMARK_AOT=4 EKA2L1_AOT_VERIFY=1024 node benchmark.ts /absolute/assets /absolute/checked 1000
EKA2L1_BENCHMARK_AOT=4 node benchmark.ts /absolute/assets /absolute/repeat 1000
EKA2L1_BENCHMARK_AOT=4 EKA2L1_GL_DIAGNOSTICS=1 node profile.ts /absolute/assets /absolute/gl-checks-on 0 0
EKA2L1_BENCHMARK_AOT=4 PROFILE_GATE=/absolute/cpu-gate node profile.ts /absolute/assets /absolute/cpu 0 1
```

From the root, warm and pause timing fixtures. Finish correctness/tests, release
the CPU gate and let profiling finish before releasing the timing gate:

```sh
python3 src/tests/benchmark/profile_batch.py --assets /absolute/assets --output /absolute/full --compare-build /absolute/baseline-frontend --measure-gate /absolute/timing-gate
# Wait for the gates' .ready files and completed correctness tests.
touch /absolute/cpu-gate
# After the CPU-profile runner exits:
touch /absolute/timing-gate
# After full-capture measurements finish:
python3 src/tests/benchmark/profile_batch.py --assets /absolute/assets --output /absolute/no-capture --compare-build /absolute/baseline-frontend --capture-mode 2
```

Each batch runs old/new/new/old serially after parallel warmup. No-capture still
renders; it disables framebuffer readback and capture output. The fixed guest
workload and execution counters must agree. Full replay comparisons supply
separate pixel/PCM correctness evidence. Sampling, verification and guest
profiling are disabled during timing controls. SwiftShader and a shared host
limit the interpretation; physical-GPU interactive gameplay and normal Qt JIT
performance are not measured here. Audio-quality work remains deferred.
