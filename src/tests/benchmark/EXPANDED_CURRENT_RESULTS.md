# Forward-only leaf expansion on the current runtime

Status: graduated as the browser default and deployed to the LAN UI. The exact
measured artifact passes real two-game gameplay checks; the existing host audio
limitation remains recorded below.

The frozen historical matching-bound comparison gains 1.96% Snakes CPU
throughput, with all four pairs faster. The current-runtime candidate restores
forward-only internal branches, multiply forms and extra/conditional scalar
memory eligibility. Backward branches and nested calls remain rejected. It
uses per-call forward labels and leaves existing caller-loop lowering unchanged.
This differs from the rejected connected-callee implementation.

The practical comparison uses the graduated runtime with its bound 16 as control
and expanded eligibility with bound 32 as candidate. This measures the combined
change available for adoption, not the isolated contribution of either setting.
Both games use frozen ABBA/BAAB observations, fixed measured frequency and the
same validity rules. There is no temperature or post-build cooldown.

Evidence and frozen artifacts are in
`/home/claude/.scratch/eka-expanded-current`. Focused interpreter comparisons
include false predicates, flags, short budgets, missing mappings, physical
aliases and the current outlined-entry-budget mode. Exact game replays precede
timing. No speed claim is made from the historical percentage alone.

The build and all targeted checks pass: 359,055 new expanded-leaf comparisons,
29,120 existing predicate comparisons, loop-budget checks and 17,280 entry-budget
comparisons. Both 60-frame replays match images, frame records and PCM exactly.
The candidate WASM SHA-256 is `08b363be84ddeb9fb86f5be92c7cc81c175773ab83e58d49a57637b198ff325b`.
Initial pre-build anchor and obsolete test-constructor errors are retained in
the scratch evidence; neither reached timing.

## Completed current-runtime comparison

| # | Game | CPU throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: |
| 1 | Snakes | +1.10% | -2.04% | 4/4 |
| 2 | Sky Force | +0.17% | -0.01% | 3/4 |

All 16 observations pass the frozen validity rules. All 88 live host-restoration
checks pass. The Snakes gain is repeatable across both orders and accompanies a
native instruction reduction. Sky Force is effectively neutral: its small mean
gain has mixed pairs and nearly unchanged instructions; no Sky Force speedup is
claimed. This earns adoption for the Snakes improvement. The exact measured runtime
artifact is served with the measured configuration after the checks below.

## Graduation and deployment

The broader compiler suite completes with 173 passed and zero failed. Its two
existing diagnostic-build skips and one known expected failure are unchanged.
Launcher-policy tests pass. The browser launcher, benchmark and profiler share
the selected defaults: predicated leaves enabled, literal-PC veneer feature 128,
and execution limits `512,32,8,512`. Existing CPU/runtime defaults remain enabled.

The LAN service serves the exact measured WASM artifact. The downloaded SHA-256
matches `08b363be84ddeb9fb86f5be92c7cc81c175773ab83e58d49a57637b198ff325b`.
Both games advance frames and consume input with the observed limit 32 and all
other selected policies. Keyboard/touch, launcher, layout and visual checks pass
on NVIDIA Vulkan; saved game and mobile screenshots were inspected.

Full browser E2E remains false solely for non-silent audio in both games. The
browser AudioContext clock stays at zero, as in the independently reproduced
pre-existing host failure; exact PCM replay remains correct. No new browser
errors or other failed checks occur. This limitation is not reported as a full
E2E pass. Evidence is in `eka-expanded-current/deployment-check.json` and
`game-picker/report.json`.

The first deployment verification encountered a transient permission race while
reading the newly started process environment. A read-only retry confirmed the
selected artifact and configuration; verification continued without restarting
the service again. The initial evidence and previous service drop-in are kept.
