# Branch veneers on the graduated forward-leaf runtime

Status: graduated as the browser default and deployed to the LAN UI. The exact
measured artifact passes real two-game gameplay and policy checks; the existing
host audio limitation remains documented below.

The frozen historical comparison completes with eight valid observations and
no invalid attempts: Snakes CPU throughput +0.76%, native instructions -0.20%,
all four pairs faster. All 88 live restoration checks pass. The original
historical timing evidence remains in `BRANCH_VENEER_RESULTS.md`.

The current candidate adds one-instruction unconditional ARM branch veneers
as feature bit 32 on top of the existing literal-PC bit 128. Both variants keep
the graduated forward-only leaf expansion and execution limits `512,32,8,512`.
Only the veneer bytes become a dependency. Caller BL and callee B both consume
exact budgets; LR is retained and the B exits precisely to its real target,
even for a target inside the caller's compiled window. The target is not
assumed or recursively compiled by this optimization.

The current control is `eka-expanded-current/candidate-build`; candidate and
full evidence are in `/home/claude/.scratch/eka-branch-current`. Focused tests
cover both disabled and enabled feature modes, ordinary and outlined budgets,
positive/negative/self/in-window targets, flags, caller paths and code aliases.
Exact replays of both games precede frozen ABBA/BAAB runtime timing. No gain is
claimed on the current build from the historical percentage alone.

The build and focused suites pass: 12,800 branch-veneer comparisons, 7,680
literal-veneer comparisons and 17,280 entry-budget comparisons. The current
configuration API and strict launcher validation now admit only masks 0, 32,
128 and 160. Launcher-policy tests pass, and both 60-frame replays exactly match
images, frame records and PCM with explicit feature/predication readback.

The first replay stopped before browser launch because the retired option was
still rejected by its harness. That failure and pre-API artifact are preserved.
Fresh correctness and performance harness copies add only mask admission;
existing archived harnesses and historical plans are unchanged. Both timing
variants use the same new reporting-harness copy. No temperature or cooldown
gate is used.

Candidate WASM SHA-256: `21396e0334589c338fa2336a148de1316ab3d9673a2d911b27df01bb9c57e91c`.

## Completed current-runtime comparison

| # | Game | CPU throughput | Wall throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes | +0.87% | +0.48% | -0.19% | 3/4 |
| 2 | Sky Force | +1.98% | +1.79% | +0.00% | 4/4 |

All 16 observations pass the frozen validity rules and all 88 live restoration
checks pass. Both games have positive means, and all four Sky Force pairs
improve. The measured runtime combination earns adoption. Sky Force's native
instruction count is effectively unchanged; these counters do not establish
that its timing gain comes from fewer executed instructions. The control and
candidate are different binaries, so the result is not a same-binary attribution
of feature bit 32 alone. No mechanism beyond the measured result is claimed.

## Graduation and deployment

The broader compiler suite passes 174 tests with zero failures; two existing
diagnostic-build skips and one known expected failure are unchanged. Both
launcher-policy tests and the real browser configuration-API smoke test pass.
The browser, benchmark and profiler now select feature mask 160, combining
single-branch and literal-PC veneers. The graduated forward-only leaf expansion
and execution limits `512,32,8,512` remain enabled.

The LAN service serves the exact measured artifact with observed mask 160 and
limit 32 in both games. Downloaded WASM SHA-256:
`21396e0334589c338fa2336a148de1316ab3d9673a2d911b27df01bb9c57e91c`.
Both games advance frames and consume input. Keyboard/touch, launcher, layout
and visual checks pass on NVIDIA Vulkan; gameplay and mobile screenshots were
inspected. No browser errors or failed checks outside live audio occur.

Full browser E2E remains false solely because the same pre-existing host audio
failure persists in both games: AudioContext time stays at zero despite received
samples. Exact PCM replays pass. This is not reported as a full E2E pass.
Evidence is in `eka-branch-current/deployment-check.json`, `browser-api.log`,
`full-compiler-check.json` and `game-picker/report.json`.

Graduated runtime commit: `0b890a26a`. The original frozen historical
plans resume from 75/109 completed comparisons and 600/872 valid observations;
all 20 earlier invalid attempts remain retained.
