# Hooked On: allow loaded games past the hot-compilation quota

Hooked On: Creatures of the Deep was the next slowest game in the
[five-game browser screen](PRE_TOUCH_UNPACED_RESULTS.md), after Asphalt.
Its main bottleneck was a RAM compiler admission limit: N-Gage exhausted the
16,384-version allowance before loading the game. The game executable then
ran entirely in the interpreter, even though the compiler supports its code.
With the admission fix, clear fishing gameplay measures **44.4–90.7
presentations/s**, versus **21.4–21.6** before. The range reflects short samples
under variable host load and animation; it is not a precise general speedup.

## Diagnosis and change

A fresh Chromium profile during Costa Rica boat movement attributed 76.28%
of the CPU worker's sampled time to `InterpreterMainLoop`, 14.47% to uncached
trusted lookup and 4.88% to `lookup_compiled`. Only 0.97% was in generated guest
code. The graphics worker spent 97.31% waiting.

After that capture, debugger enumeration found 16,028 installed RAM functions,
with the last version numbered 16,383; rejected entries also consume versions.
None belonged to the game image at `0x74e00000`, size `0x1256ac`. The final
compiled entries belonged to earlier N-Gage libraries. This explains both
the interpreter cost and the repeated failed compiled-code lookups.

`compile_at` previously applied the same lifetime quota to eager preparation
of known loaded images and to runtime hot discovery. Eager RAM preparation
now bypasses the hot-discovery limits, as ROM preparation already did. A
separate counter still limits hot RAM versions to 16,384, including rejected
translations. Eager preparation also leaves the 131,072-entry hot-candidate
map untouched. Session reset restores both allowances.

This changes compiler admission, not generated instruction semantics or the
guest clock. Mapping validation, address-space isolation, the active code-byte
policy, interpreter fallback and watchdog behavior are retained. No game or
firmware patch, instruction accounting, or launch switch is added.

## Measurement scope

Measured on 2026-10-11 using Chromium 153.0.8010.52, an Intel i7-10875H and
NVIDIA hardware graphics. One emulator ran at a time on a shared workstation
with substantial, varying VM/build load. These are short throughput samples,
not isolated peak results or a narrow confidence interval.

The game is the full 0.74 BiNPDA preservation container, with stock Nokia 5320
ROM/RPKG and the N-Gage 1.40.1557 runtime. During this campaign the production
picker selected the original signed trial; it subsequently switched to the
same full package. No game binaries are tracked in this change.

The comparison scene is Costa Rica fishing, after dropping anchor and using
the cast meter. It renders animated water, sky and the fishing rod without
menus or help overlays. The earlier diagnostic boat profile is not used as
the fishing timing baseline. Start/end screenshots are inspected for each
accepted sample. Startup, installation, navigation and profiles are excluded
from the unprofiled timing windows.

Camera, cast power and animation phase are not an exact deterministic replay.
Each build has one fresh browser launch with two adjacent windows, rather
than independently repeated launches or an ABBA campaign.

FPS uses `eka2l1_presentations`, the same presentation counter as the web UI;
it is not monitor refresh or a count of deduplicated images. Both builds use
the default compiler configuration and count-free execution. The existing
route API enables unpaced execution with a distant guest-time deadline;
accepted windows have no route pauses, instruction totals or fatal/page errors.
Browser sound output is muted while guest audio processing remains enabled.

| # | Capture order | Build | Presentations | Wall seconds | Presentations/s | Emulator compilation ms |
| ---: | --- | --- | ---: | ---: | ---: | ---: |
| 1 | B1 | Candidate | 736 | 8.1175 | 90.668 | 1.622 |
| 2 | B2 | Candidate | 367 | 8.2661 | 44.398 | 0.000 |
| 3 | A1 | Baseline | 172 | 8.0513 | 21.363 | 0.000 |
| 4 | A2 | Baseline | 174 | 8.0502 | 21.614 | 0.000 |

Candidate windows had host one-minute load averages of 11.78 and 15.67,
versus 2.41 and 2.30 for baseline. These measurements support the large
improvement and removal of the interpreter bottleneck, but not a precise
multiplier. Compilation times sum emulator translation, emission and
installation; V8 compilation is not separately measured.

The fishing profiles provide an independent mechanism check. Interpreter
self-time falls from **77.41% to 1.08%**; generated guest code rises from
**0.75% to 57.72%**. The largest remaining runtime bucket is `execute_chain`
at 30.84%. The two hottest generated entries, `0x74eae2a4` and `0x74eaf424`,
belong to the Hooked On executable and account for 11.55% and 10.84%. These
are CPU-worker sampled-time shares, not aggregate browser CPU percentages.

The measured cost is additional compilation and memory for code that the old
limit refused. Total installed functions, including ROM, rise from 87,825 to
149,010. The largest renderer's resident memory during the unprofiled windows
rises from about **2.77 GiB to 3.24 GiB** (roughly **480 MiB**). This is total
renderer RSS, including WASM memory, V8 code and browser allocations, not an
isolated cache allocation. Mobile memory headroom has not been tested.

[Machine-readable results](hooked-aot-capacity-results.json) retain the exact
timings, counter endpoints, profile summaries, raw artifact paths and caveats.

## Verification and artifacts

The new `--precompile-only` capacity regression fails against the old runtime
at exactly 16,384 of 16,385 prepared functions. It passes with this change and
covers early/late entry execution, unchanged CPU state during preparation,
address reuse in another process, hot discovery after eager preparation,
the retained hot quota, eager preparation after that quota is exhausted, and
quota reset. The existing full WASM compiler suite passes **183 tests, 0
failures**; the focused capacity test is additional to that suite.

```sh
cmake --build build-wasm --target test_aot_wasm eka2l1_wasm -j4
node build-wasm/src/tests/aot/test_aot_wasm.js --precompile-only
node build-wasm/src/tests/aot/test_aot_wasm.js
```

Raw profiles, screenshots, frozen candidate build, test logs and the interactive
`run.mjs` harness are under
`/home/claude/.scratch/eka-hooked-perf-20261011/`. The harness uses production
`server.ts` and the normal compiler defaults. Both builds load identical stock
firmware and the same preserved game container.

The frozen candidate is served on `https://claude-laptop.lan:8188/`. A fresh
Chromium visit verifies its versioned WASM response, cross-origin isolation,
module initialization and upload UI without page errors (`lan-smoke.json`).
The downloaded WASM digest matches the tested artifact. Candidate gameplay
validation uses that binary through the private production server; baseline
gameplay uses the previous LAN build. The post-deployment browser check is
launcher initialization.

Source baseline: `42453a1a9`.
Baseline WASM SHA-256:
`0897120710c85c17e04762902fa34d471d98cf5160b79ad4bdeff27497bf0010`.
Candidate WASM SHA-256:
`b902463bc642ba5e31fbd585ff961e0f97d967eb9f95e2cc97ded3bb0c993284`.

Rejected captures are retained with explicit exclusion records: `baseline-a1`
was still in name entry; `candidate-b1` and `candidate-b2` encountered recurring
boat help; `candidate-final/fishing-1` encountered the cast tutorial. Their FPS
values are not gameplay speedup evidence.
