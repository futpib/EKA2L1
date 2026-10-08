# Tail prefixes on the graduated branch-veneer runtime

Status: graduated as the browser default and deployed to the LAN UI. The exact
measured artifact passes real gameplay and policy checks; the existing host
audio limitation remains documented below.

The historical comparison completes with eight valid observations and no invalid
attempts: Snakes CPU throughput +0.99%, native instructions -1.18%, three of four
pairs faster. All 88 live restoration checks pass. This warrants a current-build
comparison, not adoption from the historical percentage alone.

The candidate adds bounded register-only prefixes ending in unconditional ARM B,
using feature bit 64 on top of mask 160, with execution limits `512,32,8,512`.
Fully supported returning leaves retain priority over shorter tail prefixes.
The final B retains the precise dispatch exit, LR, instruction count and target,
including targets inside the caller region. Only executed prefix bytes become
code dependencies. Conditional register operations consume exact budgets;
SP/LR/PC operands, memory, multiply and nested control flow remain excluded from
the prefix fallback. Existing full-leaf eligibility is unchanged.

Control: `eka-branch-current/candidate-build`; candidate and frozen evidence:
`/home/claude/.scratch/eka-tail-current`. Fresh harness copies admit bit 64 and
its combinations without changing prior archives or historical plans. Both games
will use ABBA/BAAB under measured fixed frequency, affinity/isolation and hardware
counter validity checks. No temperature or cooldown gate is used.

The candidate passes 115,200 tail-prefix state/budget/alias comparisons, 12,800
branch-veneer comparisons, 7,680 literal-veneer comparisons and 17,280 entry-budget
comparisons. Both disabled and combined feature modes are covered. A selection
check confirms fully supported returning leaves remain intact. Launcher-policy
and real browser configuration API checks pass. Both 60-frame replays exactly
match images, frame records and PCM, with explicit selector and artifact checks.

Candidate WASM SHA-256: `4bdb01dab90af78db286ff529d5a25a5160cf51700550b3d3899c4193fb2a86a`.

Both-game runtime timing is complete; the results below determine adoption.

## Completed current-runtime comparison

| # | Game | CPU throughput | Wall throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes | +2.32% | +2.06% | -1.03% | 4/4 |
| 2 | Sky Force | +0.68% | +0.74% | +0.01% | 3/4 |

All 16 observations pass the frozen validity rules, with no invalid attempts.
All 88 live restoration checks pass. Snakes gains range from +0.90% to +5.51%;
the slower closing control in the first panel is retained and contributes to
the mean. Sky Force pairs range from -6.63% to +4.28%, with essentially unchanged
native instructions. Its positive average does not establish a reliable gain.

The candidate is adopted for the consistent Snakes improvement and positive two-game
means. This is an end-to-end comparison of different runtime binaries, not a
same-binary attribution of feature bit 64 alone. Final compiler, default-launcher
and real LAN gameplay checks are complete, with the audio limitation below.

## Graduation and deployment

The full compiler suite passes 175 tests with zero failures. Two existing
diagnostics-only skips and one known expected failure are unchanged. Browser,
benchmark and profiler defaults select mask 224: branch, register-tail and
literal-PC veneers, retaining the graduated forward-only leaf expansion and
execution limits `512,32,8,512`. Strict configuration validation admits only
combinations of feature bits 32, 64 and 128.

The LAN service serves the exact measured artifact and both games report mask
224 and bound 32. Downloaded WASM SHA-256: `4bdb01dab90af78db286ff529d5a25a5160cf51700550b3d3899c4193fb2a86a`.
Gameplay advances frames and consumes input in both games. Keyboard/touch,
launcher, layout and visual checks pass on NVIDIA Vulkan. Saved gameplay and
mobile screenshots were inspected. No browser errors or non-audio check failures
occur.

Full browser E2E remains false solely because the same pre-existing host audio
failure persists in both games. Exact PCM replays pass; this is not reported as
a full E2E pass. Evidence is in `eka-tail-current/deployment-check.json`,
`full-compiler-check.json`, `browser-api.log` and `game-picker/report.json`.
