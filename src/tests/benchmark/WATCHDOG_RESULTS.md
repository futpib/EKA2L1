# Count-free watchdog execution

This opt-in experiment removes guest instruction accounting from normal DynCom
execution and generated ARM/Thumb code. It is disabled by default. Set
`EKA2L1_WATCHDOG_US=2000` in the browser launcher or benchmark environment.
The value is the external worker's host-time request period, not a guaranteed
maximum execution duration. Verification and instruction diagnostics must be off.

## Execution and clocks

A dedicated JavaScript worker periodically stores an atomic flag in the shared
WASM memory. Generated code reads it directly; polling does not call JavaScript.
The compiled runner polls between indirect successor dispatches. ARM regions
poll at internal backward-branch targets unless static analysis proves the loop
terminates. The initial proof recognizes a pure unconditional ALU body followed
by `SUBS counter,counter,#1; BNE`, with no other counter or flag writes, helpers,
memory operations or alternate branches. Modular decrement terminates for every
32-bit input. A zero input can therefore run for billions of iterations without
polling: this is the requested finite-region policy, not a responsiveness bound.
Bounded Thumb bodies are acyclic; their successor dispatch supplies the poll.

The interpreter likewise polls at dispatch boundaries. Compiled exits return
progress status, preserving the zero sentinel for pending syscalls and deferred
instructions. Single stepping still uses the counted interpreter. The ordinary
counted mode remains the regression reference.

The host watchdog and guest timers have different jobs. The watchdog regains
control from unproved execution. Guest deadlines determine which emulated event
is due. There are now two clock policies, both without instruction accounting:

- Paced browser play samples elapsed monotonic host time at watchdog/scheduler
  boundaries and while idle. The request flag does not assign a fixed amount of
  guest time. Explicit emulator pauses reset the host origin; gaps over 100 ms
  are discarded to avoid replaying timers after a browser stall. Idle execution
  sleeps for 1 ms between checks. The legacy instruction-clock pacing sleep is
  disabled for this mode.
- Unpaced benchmarks and diagnostic route exploration keep virtual CPU slices.
  A request advances to the next event, capped by the running thread's remaining
  virtual slice; idle execution jumps to the next event. This clock does not copy
  elapsed host time. How much computation fits into a slice depends on host
  execution and compilation, so it is approximate and nondeterministic.

Both policies charge elapsed guest time to the scheduler's existing tick field,
without counting executed instructions. The 2 ms request period was not optimized.

A guest-time-only watchdog cannot solve the busy-loop case if that loop prevents
guest time from advancing. Other choices are coarse work/time charges at selected
guest boundaries, or externally specified/replayed event delivery. Both require
an explicit model; removing instruction accounting does not itself define how
much virtual time arbitrary computation consumes.

## Game output comparison

Fixed guest-time windows were inappropriate for comparing these clocks. The
original startup replays reached different menus/game phases. Shortened startup
inputs now end before the measured sequence, and an exact rendered gameplay
frame starts the capture. Both variants then produce 120 unique Snakes frames
with no further input. `EKA2L1_BENCHMARK_START_FRAME` supplies a reference PNG to
the harness; the dumper waits for its RGB pixels before starting its existing
capture timer. This does not count guest instructions or change guest time.

Four fresh Chromium runs used normal V8 tiering, direct memory, hardware GPU,
shared audio and serial ABBA order. These are exploratory elapsed measurements,
without frequency isolation or worker CPU counters. Pixel readback and PNG
encoding are included equally; they are not measurements of live-play FPS.

| # | Order | Mode | 120-frame capture seconds | Frames identical to counted reference | Maximum pixels differing by more than 8/channel |
| ---: | --- | --- | ---: | ---: | ---: |
| 1 | A | Counted | 3.19849 | 120 | 0% |
| 2 | B | Count-free, 2 ms watchdog | 2.61164 | 18 | 1.405% |
| 3 | B | Count-free, 2 ms watchdog | 2.57705 | 17 | 1.164% |
| 4 | A | Counted | 3.20390 | 120 | 0% |

Mean captured-frame throughput improves **23.4%** (3.20120 s to 2.59435 s).
Every frame passes the repository's pre-existing comparator: at most 2% of
pixels may differ by more than 8 in a color channel. That is visual equivalence
under this tolerance, not identical output or a full game-state comparison.
Both counted runs are pixel-identical throughout. The count-free runs have small
differences in HUD/object animation that grow through the window.

The counted sequence spans 5.59535 guest seconds; the count-free sequences span
7.05 and 7.004375. All count-free instruction totals remain zero. Thus the result
credits faster production of visually comparable gameplay frames, while also
exposing a timing-fidelity tradeoff. It cannot isolate compiler gains from the
changed scheduling policy or establish equivalent timer-sensitive behavior.
No full-state checkpoint/restore comparison is claimed. Longer gameplay and
timer-sensitive interactions remain unverified.

Sky Force also reaches gameplay with the extended startup route and zero
instruction totals. Its timing was not compared on equivalent game output.

## Paced Sky Force follow-up

The first ordinary interactive launch exposed a starvation bug. The 2 ms
watchdog continued requesting yields during host pacing sleeps. On the next
CPU entry, the pending request could immediately return without guest execution,
yet the scheduler charged another virtual slice. Sky Force stayed blank with
zero presentations after more than 68 guest seconds. Changing only the external
request period to 20 ms allowed startup to progress.

The scheduler now clears requests raised outside guest CPU execution immediately
before starting a normal CPU slice. Requests raised during execution still reach
the existing safepoints. This changes the experimental event-clock path only.

With that fix and the original 2 ms watchdog, the actual browser launcher reached
Stage 1 combat. A 34.77445-second host window advanced 34.77 guest seconds and
produced 522 presentations: **0.99987x guest-clock realtime, 15.01 presentations
per second**. Approximately ten-second subwindows ranged from 10.1 to 17.2
presentations per second; a final four-second segment reached 18.3. Screenshots
show planes, scrolling scenery, combat and increasing score. Instruction totals
remained zero. Presentation counts are not an oracle for unique frames or game
state, and no stock-device frame-rate comparison was made.

The host audio device failed and suspended its Web Audio context. Switching the
test browser to a silent, clocked Web Audio output allowed the production audio
worklet to run. Across 24.546 seconds it reported zero new underruns, one resync
and 5,792 discarded sample frames. This verifies non-silent PCM consumption in
the worklet, not physical speaker playback or perfectly continuous sound.

These are functional observations on a busy host, not isolated throughput
measurements. The first capture overlapped CPU tests and ended later with a
detached Puppeteer frame; its cause remains unresolved. A second fresh launch
under greater host load reached menus, but its fixed startup inputs no longer
matched the slower startup and it is not counted as another combat measurement.
The rebuilt focused watchdog tests passed. An additional full CPU-suite rerun
exited with status 143 before completion; the earlier full-suite result above
belongs to the previous revision.

Pacing therefore runs actual Sky Force gameplay after the fix. It does not yet
establish original-device game speed: the experimental virtual-slice clock can
stay realtime while allowing different amounts of computation between timers.
See `WATCHDOG_PACED_EVIDENCE.json` for endpoints, configuration and limitations.
The LAN server was left on its existing build.

### LAN startup regression and recovery

The experiment was subsequently deployed to the LAN launcher, enabling it for
both games. A user reported Snakes still blank after five minutes. Their log
showed successful WebGL setup and slow progress through `AknIconSrv` startup,
not a graphics-initialization failure.

Fresh browser checks against the deployed build reproduced severely delayed
visible output: approximately 56 seconds for a Snakes splash in Chromium and
116 seconds in Firefox. With the same WASM and the experiment disabled only in
a private Chromium test browser, a splash was visible by the 10-second sample.
These timings include startup and five-second screenshot sampling, differ in
host load, and do not establish a precise speed ratio. The five-minute user
case was not reproduced locally.

The LAN service was restored to counted execution by setting
`EKA2L1_WATCHDOG_US=0`; the WASM build was retained. A fresh live Chromium
launch, with no browser-side policy override, showed the Snakes splash by the
10-second sample. Existing tabs require reloading because configuration is
frozen before initialization. Raw logs, screenshots and the saved service
configuration are under `/home/claude/.scratch/eka-watchdog/blank-lan`.

At this recovery point the watchdog/virtual-slice mode remained an opt-in
experiment with a known paced startup regression. The earlier Sky Force-only
deployment check was insufficient coverage for enabling it across the game picker.

### Browser count-free clock repair

Clearing old requests fixed zero-work preemption but left the clock policy wrong
for live play. A roughly 2 ms host interval could consume a whole virtual CPU
slice; the frontend then slept to repay that artificial time advance. Startup
performed too little guest computation between sleeps. Paced count-free play now
uses the host-clock policy described above. Requests arriving in runtime work
still trigger a clock sample, while requests predating CPU entry cannot prevent
that entry from making progress. The unpaced virtual-slice policy remains intact.

Hardware Chromium checks exercised actual Snakes gameplay and Sky Force Stage 1
combat, keyboard input and the production audio worklet with a silent clocked
sink. Every sampled instruction total remained zero and the 2 ms watchdog stayed
enabled. The ordinary frontend was used, with no benchmark configuration.

| # | Gameplay window | Host seconds | Guest/host time | Presentations/s |
| ---: | --- | ---: | ---: | ---: |
| 1 | Snakes, fresh | 8.199 | 1.00000 | 16.10 |
| 2 | Snakes, after 250 ms renderer suspension | 8.161 | 0.99993 | 16.05 |
| 3 | Snakes, after 6 s renderer suspension | 8.186 | 1.00002 | 16.00 |
| 4 | Snakes, settled | 8.176 | 1.00001 | 16.02 |
| 5 | Sky Force, combat | 8.274 | 1.00020 | 31.91 |
| 6 | Sky Force, settled combat | 8.173 | 1.00007 | 32.06 |

Recovery checks also bound one-second windows against fast-forwarding and verify
input still works. Both settled audio windows had nonzero samples and no new
underruns, drops or resyncs. Physical speaker playback was not tested. Frame rates
above count presentations, not unique frames or equivalence with a stock phone.
These are functionality/pacing checks, not an isolated compiler speedup result.

An unpaced smoke replay captured 30 unique Snakes frames in 0.653 host seconds
while guest time advanced 1.490 seconds, with zero instructions. This verifies
that count-free execution still runs unpaced; it is not an A/B speed measurement.
Four focused native timer cases passed 32 assertions, including event delivery,
cancellation, callback rescheduling, pauses, stall recovery and reset. The rebuilt
WASM watchdog tests also passed finite-region/proof rejection and external
atomic interruption checks.

The frozen build and active watchdog configuration were restored on the LAN
service. Fresh Chromium checks through the actual game picker showed Snakes by
10.00 seconds and Sky Force by 11.52 seconds, with working input and zero
instruction totals. The first picker replay reached Snakes gameplay but stopped
at Sky Force's ship-selection menu; a separate longer Sky Force replay verified
Stage 1 combat, directional input, scrolling scenery and increasing score.
A fresh Firefox startup check
also displayed Snakes, by its 35.93-second sample; its screenshot calls add about
ten seconds between observations, so this is not a precise startup comparison.
The served WASM SHA-256 matches the frozen build. These checks use versioned
assets and no browser-side compiler policy override.
See `WATCHDOG_HOST_CLOCK_EVIDENCE.json` and local artifacts under
`/home/claude/.scratch/eka-watchdog/host-clock`. Reload existing tabs to load the
new build and initialization policy.

## Equal-work mechanism check

Two synthetic loops each perform 50 million iterations. The counted and
count-free variants produce identical integer register outputs. Dispatch stays
inside WASM, with ordinary indirect WASM calls, rather than crossing JavaScript
per region. The memory fixture uses TLB access; neither fixture represents the
whole game's region distribution. The asynchronous watchdog is not running in
these timings: the unproved loop polls a clear atomic flag. Actual asynchronous
interruption is tested separately.

Final ABBA/BAAB measurements requested 2.4 GHz, reserved CPU 7 and sibling 15,
and measured user cycles/reference cycles and native instructions. All 16 final
observations met the predeclared 0.5% mean-frequency tolerance with unmultiplexed
counters. Policies and CPU placement were restored. Earlier attempts at 3.6 GHz
and 2.4 GHz had invalid clock observations; every observation is retained in
`WATCHDOG_EVIDENCE.json`. The controller supports up to three retries of the same
variant after a failed clock observation and does not reject valid slow results.

| # | Loop | Counted mean ms | Count-free mean ms | Throughput ratio | Native instruction change |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Proved finite ALU loop | 200.075 | 114.983 | 1.740x | -42.04% |
| 2 | Unproved memory loop, polling | 451.247 | 428.621 | 1.053x | -4.55% |

These establish a benefit for the tested execution shapes. They do not predict
a 74% game speedup or measure periodic preemption overhead.

## Validation and reproduction

The ordinary WASM suite reports **186 passed, 0 failed**; existing diagnostics-only
skips remain. Focused watchdog tests cover expired flags in finite ARM/Thumb
bodies, the static termination proof and its rejection, pending Thumb SVC's zero
return, and interruption of an infinite loop by a separate JavaScript worker.
The actual browser launcher starts its worker, freezes configuration and advances
guest time. Both games were inspected in gameplay.

With the experiment disabled, the frozen pre-change browser build and the new
build match all 60 Snakes capture PNGs, guest-time/instruction journals, PCM and
audio events exactly. The asynchronous `visible-first.png` screenshots differed;
those are not the synchronized game-frame oracle.

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j4
node build-wasm/src/tests/aot/test_aot_wasm.js --watchdog-only
node build-wasm/src/tests/aot/test_aot_wasm.js --watchdog-kernels
```

For game comparison, obtain the marker by capturing 500 counted frames from
guest time zero using `snakes.input`; frame 260 is the marker in this recorded
route. Its SHA-256 is retained in the evidence. Then run `benchmark.ts` with 120
frames, start time zero, and `EKA2L1_BENCHMARK_START_FRAME=/absolute/marker.png`.
Use `watchdog-snakes-counted.input` for the counted run and
`watchdog-snakes-countfree.input` with `EKA2L1_WATCHDOG_US=2000` for the experiment.
Compare the resulting PNGs with `compare-frames.ts`. Use `capture_wall_seconds`
from `metrics.json`; the harness's overall elapsed field also includes startup
and artifact extraction. If the marker or output sequence does not match, the
run is not comparable and must not silently become a throughput result.

For kernel counters, run `watchdog_kernels.py TEST_JS NEW_OUTPUT --mhz 2400`
inside `fixed_frequency.py --khz 2400000 --isolate-cpus 7,15`, following
`CONTROLLED_BENCHMARKS.md`. The reference counter frequency, CPU selection and
platform profile in this evidence belong to this host.

Raw local artifacts are under `/home/claude/.scratch/eka-watchdog`. ROM, game
bytes and screenshots remain outside Git. Nothing was deployed to the LAN site.

## Paced LAN comparison and counting selector (2026-10-09)

The LAN launcher now offers **Instruction counting: On / Off** beside the game
selector. **Play** applies the choice by starting a fresh session. The URL keeps
`counting=on` or `counting=off`; the running status reports the actual mode.
Other tabs and the server default are unaffected. The HTTP policy tests cover
both defaults, override isolation, invalid values and policy-dependent ETags.
Real browser runs exercised both switch directions and checked instruction
totals, watchdog state/worker, applied configuration and keyboard input.

The following are 30-host-second gameplay observations through
`https://claude-laptop.lan:8188/`, using headless Chromium 153.0.8010.52 and
Firefox 156.0 on an i7-10875H. Browser sound output was muted. Each test used a
fresh profile; owned test browsers ran serially. Screenshots before and after
each window were visually checked for gameplay. They were captured outside the
timing window. Rates count presentations, not unique frames.

| # | Browser | Game | Counting on presentations/s | Counting off presentations/s | Counting on guest-time ratio | Counting off guest-time ratio |
| ---: | --- | --- | ---: | ---: | ---: | ---: |
| 1 | Chromium | Snakes | 21.18 | 16.00 | 1.000 | 1.000 |
| 2 | Firefox | Snakes | 21.17 | 16.03 | 1.000 | 1.000 |
| 3 | Chromium | Sky Force | 25.48 | 32.01 | 0.796 | 1.000 |
| 4 | Firefox | Sky Force | 21.52 | 31.98 | 0.673 | 1.000 |

These are playability observations, not controlled compiler speedup ratios.
Counting on uses instruction-driven guest time; counting off uses elapsed host
time for paced guest timers. A 1x count-free clock alone does not establish
sufficient CPU throughput or stock-device timing equivalence. Background host
work continued, without fixed frequency or CPU isolation. Chromium reported
the NVIDIA Quadro T1000 Vulkan renderer; Firefox exposed a privacy-masked WebGL
renderer, which does not establish its physical GPU path.

The initial Firefox Sky Force counted run overlapped a separate Rust build and
measured 13.74 presentations/s at 0.429x guest time. The table uses its repeat
after that build ended; both observations are retained. The first Firefox
count-free Sky Force run stayed on ship selection and was rejected as a gameplay
measurement, despite its 32.00 presentation rate. Its repeat held menu keys for
600 ms, matching the existing game-picker test's hold duration, and reached
Stage 1 combat. This final repeat loaded another session's newly deployed touch
UI, with a larger displayed canvas. All runs used identical emulator WASM,
loader JS, data, audio and game assets, but this UI difference is an additional
comparison confound. The raw evidence records each case's asset URLs.

The Firefox Snakes counted measurement, mode assertions and both screenshots
completed, but an optional subsequent `about:support` GPU diagnostic was rejected
by Firefox BiDi. Its raw error is retained; that diagnostic was removed from the
harness. No timing result depends on successful navigation to that page.

ARM-to-WASM compilation is included whenever it happens during the FPS window.
The repeated Firefox Sky Force runs installed **441** additional translated
functions with counting on and **220** with counting off. The existing installed
function counter was added to the harness from the Firefox Sky Force tests
onward; earlier tests did not retain it. These counts do not measure translation
duration or rejected attempts. Thus the windows are not certified to be free
of compilation. Startup ROM translation and menu traversal occur before timing.

Reproduce with `src/tests/wasm/counting-comparison.ts` as documented in the
[browser launcher README](../wasm/README.md). Full numeric observations,
rejected-menu evidence, configuration, hashes and local artifact paths are in
[`COUNTING_BROWSER_COMPARISON.json`](COUNTING_BROWSER_COMPARISON.json).
The emulator WASM SHA-256 is
`8ba81e0cbac677d49eaef4f651fa4e208bc4759821e8653703726facb595efbe`.
