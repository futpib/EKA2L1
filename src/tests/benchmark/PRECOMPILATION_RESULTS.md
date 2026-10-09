# Preparing loaded code before guest execution

The launcher now prepares additional ARM/Thumb translations when executable
images load. On this host, moving that work earlier did **not** remove the
Snakes presentation-rate difference between counting modes: counting on
measured 21.19 presentations/s and counting off 16.00. The count-free window
spent just 7.7 ms in measured compilation stages over 30.19 seconds.
Ongoing EKA2L1 compilation therefore does not explain that sustained gap here.

These are host-specific presentation rates. They do not establish input
latency, equivalent game progression, perceived smoothness, or a ranking for
other devices. Counting off changes both execution overhead and guest timer
scheduling; its elapsed-host-time clock can advance at 1x even when the emulator
has insufficient CPU throughput. Both modes remain selectable.

## Implementation

The loader queues entries after relocation and import patching. The CPU worker
prepares them before its next run, using the current address space. It starts
from image entry points, RAM exports and candidate code pointers in RAM images,
then follows static dispatch exits, call continuations and word-literal
references. Candidates must lie in known loaded executable image extents.
Literal values are discovery hints, never constants substituted into execution.
Data embedded in text can still produce unused candidates.

The existing initial EUser/FntStore compilation remains. Preparation extends
coverage through the normal translator, cache and module installer. No guest
code executes to warm up the cache. Loader notifications retain numeric
addresses, not guest backing pointers. RAM compilation resolves the current
executable mapping and uses the existing address-space/backing/dependency
checks. Existing hot compilation thresholds and cache limits remain.

Later library loads receive another preparation pass. Targets that cannot be
discovered statically retain hot compilation. This is not a promise that all
compilation finishes before the first presentation, nor a persistent cache
across reloads. The initial prototype scanned every loaded ROM DLL's exports
and pointer candidates: it installed 255,694 regions before timing and had
excessive startup/memory cost. That implementation was discarded. The retained
pass reaches ROM libraries through entries and references instead.

## Chrome measurements

Four fresh Chromium 153.0.8010.52 sessions ran serially against the same frozen
bundle at `https://claude-laptop.lan:8188/`, on the i7-10875H/NVIDIA Quadro T1000
host. Each timed window was approximately 30 host seconds, sound muted,
900x800 viewport. Both endpoint screenshots were inspected: Snakes Level 1 and
Sky Force Stage 1 combat. Screenshots and menu inputs occur outside timing.
Other optimizations stayed enabled, including direct memory, unsafe code mode
3, IR mode 17, compiled syscalls, sparse ROM lookup and entry budgets. The
count-free watchdog interval was 2 ms.

| # | Game | Counting | Presentations/s | Guest clock | Compilation ms | Window share | New regions | Max observed gap ms |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Snakes | On | 21.19 | 1.000x | 228.2 | 0.756% | 586 | 90.1 |
| 2 | Snakes | Off | 16.00 | 1.000x | 7.7 | 0.026% | 60 | 83.5 |
| 3 | Sky Force | On | 27.72 | 0.867x | 169.5 | 0.561% | 349 | 167.6 |
| 4 | Sky Force | Off | 32.00 | 1.000x | 6.2 | 0.021% | 34 | 52.6 |

Compilation sums elapsed time in EKA2L1 translation, WASM module emission and
synchronous installation. Initial translation totals also include ROM scanning.
Installation includes the synchronous WebAssembly module/instance/table work;
these counters do not attribute later V8 tier optimization. Discovery, mapping
lookup and bookkeeping are not separately timed. None of this time is removed
from the FPS denominator. Frame gaps come from RAF observations of the
presentation counter, not precise GPU timestamps or unique-frame detection.

The previous Chrome observations were 21.18/16.00 for Snakes and 25.48/32.01
for Sky Force (on/off). Host work continued, including the translator test suite
during this campaign, without fixed frequency or CPU isolation. The UI and
menu traversal also changed. The counted Sky Force difference is therefore not
a controlled compiler-speedup claim.

## Loading and memory costs

| # | Game | Counting | First presentation s | Regions at first presentation | Regions at timing start | Prepared regions at timing start | Compilation before timing s |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Snakes | On | 14.19 | 20029 | 45560 | 35485 | 11.51 |
| 2 | Snakes | Off | 8.96 | 19989 | 44408 | 35511 | 5.85 |
| 3 | Sky Force | On | 12.18 | 18785 | 42296 | 34266 | 9.68 |
| 4 | Sky Force | Off | 8.41 | 18713 | 41701 | 34265 | 5.38 |

First-presentation time starts at navigation and includes asset loading/device
installation. It may be a blank presentation, not the first visible game image.
Libraries can load after that point. These observations demonstrate earlier
preparation, not faster boot versus the old build. Prepared-region counts are
additional to the initial ROM export pass and include candidates never used.

Frontloading uses additional compiled-code memory. The emulator reported about
877–879 MB of allocated heap at timing start. One counted Sky Force renderer
snapshot showed 1.81 GiB RSS; this is neither a peak nor a paired memory baseline.
The raw snapshot is `final-renderer-memory.json` in the artifact directory.

## Validation and reproduction

The ordinary WASM translator suite passed **186 tests, 0 failures**, with its
existing diagnostics-only skips. Focused preparation tests cover static exits,
ARM/Thumb literal veneers and callbacks, process isolation, deferred RAM images,
later loads and unchanged guest CPU state. Focused watchdog tests also passed.
All four live browser runs checked the selected mode, instruction totals,
watchdog worker, input consumption, applied policy and absence of browser or
module-instantiation faults.

The input script now waits for a presentation and spaces menu presses relative
to the current guest clock, with 600 ms holds. One earlier trial landed on the
Snakes help animation and was rejected despite a 16.01 presentation rate. A
probe-before-initialization failure and a preview-server startup race were also
retained. The evidence lists these trials separately.

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j6
node build-wasm/src/tests/aot/test_aot_wasm.js
node build-wasm/src/tests/aot/test_aot_wasm.js --precompile-only
node build-wasm/src/tests/aot/test_aot_wasm.js --watchdog-only
node src/tests/wasm/counting-comparison.ts https://claude-laptop.lan:8188/ /absolute/new-output chrome snakes on 30
```

Repeat the last command for both games and both counting modes, serially, and
review the saved game scenes. Numeric evidence, policies, asset hashes, window
endpoints and rejected-trial paths are in
[`PRECOMPILATION_BROWSER_COMPARISON.json`](PRECOMPILATION_BROWSER_COMPARISON.json).
Raw artifacts remain at `/home/claude/.scratch/eka-precompile`.
The measured/deployed emulator WASM SHA-256 is
`30f6c904c1530707800100f45cb8c015c08750c15975ed22caffaff7b8a9b172`.
The later FPS overlay is a separate HTML/CSS change and retains this emulator
binary; it was not present during the four timed windows above.

The overlay was checked in a real Chromium session at 390x844: hidden on the
picker, visible within the viewport during execution, no horizontal overflow,
and no pointer interception. Its displayed values matched the presentation
counter samples in both counting modes. Count-free Snakes was inspected in
gameplay; the counted overlay check sampled startup. Evidence and screenshots
are in `fps-smoke/` under the raw artifact directory.
