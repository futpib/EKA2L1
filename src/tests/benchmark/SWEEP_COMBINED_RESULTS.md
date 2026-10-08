# Completed sweep and combined graduation measurement

Status: all 109 historical game comparisons are complete, with 872 valid
observations and 20 retained earlier invalid attempts. The final combined
comparison adds 16 valid observations and no invalid attempts. Host settings
are restored; no benchmark or graduation is queued.

The combined additions since 632c3d433 improve Snakes CPU throughput 2.56% with all four pairs faster, but reduce Sky Force throughput 1.80% with two faster and two slower pairs. This is a measured tradeoff, not a demonstrated two-game win. Every valid observation is retained. The closing comparison makes no further promotion or rollback; the previously graduated runtime 7f5f85273 remains deployed. The Sky Force loss remains an explicit limitation requiring targeted isolation before claiming an overall two-game improvement.

## Combined runtime measurement

Control: recovered-defaults commit `632c3d433`, feature mask 128 and limits
`512,16,8,512`. Candidate: deployed commit `7f5f85273`, feature mask 224 and
limits `512,32,8,512`. This measures forward-only leaf expansion, the raised
leaf bound, branch veneers and register-only tail prefixes together. Both
artifacts already contain compiled syscalls, sparse ROM lookup and outlined
entry budgets. This is the gain since that earlier promotion, not a comparison
with the repository before those earlier changes.

| # | Game | Control CPU seconds | Candidate CPU seconds | CPU throughput | Wall throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Snakes | 7.706739 | 7.514116 | +2.56% | +2.21% | -3.22% | 4/4 |
| 2 | Sky Force | 24.534814 | 24.985452 | -1.80% | -1.66% | -0.01% | 2/4 |

Snakes pairs range from +2.15% to +3.07%. Sky Force pairs are +1.12%,
-4.93%, -4.19% and +1.04%. Its native instruction count is almost unchanged.
The two slower candidate observations passed all frozen validity checks; they
are included without exclusions or temperature-based explanations. Four pairs
do not establish that a small gain or loss generalizes. Individual graduation
percentages must not be added to estimate this combined result.

## Graduations and retained decisions

| # | Change | Runtime commit | Individual current-runtime evidence |
| ---: | --- | --- | --- |
| 1 | Forward-only leaves and browser bound 32 | `21c322713` | [Snakes +1.10%; Sky Force +0.17%, mixed](EXPANDED_CURRENT_RESULTS.md) |
| 2 | Single-branch veneers | `0b890a26a` | [Snakes +0.87%; Sky Force +1.98%](BRANCH_CURRENT_RESULTS.md) |
| 3 | Register-only tail prefixes | `7f5f85273` | [Snakes +2.32%; Sky Force +0.68%, mixed](TAIL_CURRENT_RESULTS.md) |

The earlier [recovered-defaults round](RECOVERED_DEFAULTS_RESULTS.md) adopted
compiled syscalls, sparse ROM lookup and outlined entry budgets. Its own
baseline and measurements remain separate. The table above records the
individual decisions; the combined result above takes precedence for claims
about the full current combination.

The final historical direct-versus-TLB comparison confirms direct: Snakes
+29.35% CPU throughput and Sky Force +14.96%, all four pairs faster for each.
Direct was already the default; these are not additional gains from this round.
TLB scalar alignment-guard removal loses 1.13% in Snakes and 0.41% in Sky Force
and remains rejected. Allocation ranges and the other retired cache variants
have their full gains, losses and tradeoffs in the [central index](EXPERIMENT_INDEX.md).

The bounded ROM-call port was validated and then rejected on current-runtime
timing: [Snakes -1.45%, Sky Force +0.26%, only one faster pair each](ROM_CALLS_CURRENT_RESULTS.md).
Its source was restored. The main build outputs were rebuilt and match all six
files of the deployed, validated tail-prefix artifact exactly.

## Controls, validation and evidence

ABBA then BAAB, four fresh observations per variant per game. The request is
3.6 GHz; measured effective clock is about 3591.56-3591.61 MHz. Worker CPU 7
and sibling 15 are reserved, with support work on the other cores. Measured
clock stability, throttle counters, affinity, identical guest work and hardware
counter validity remain required. There is no temperature ceiling or cooling
wait; a detected build requires only one second clear before timing resumes.

Both runtime artifacts are unchanged from their correctness-tested versions.
All twelve frozen runtime-file hashes were checked before timing. The current
source guard matches committed graduation defaults. The candidate already
passed 175 compiler tests, exact image/frame/PCM replays, and real LAN gameplay
and input checks. Full browser E2E remains false because of the previously
reproduced host audio-device failure; this is not reported as a full E2E pass.

All 88 post-run checks confirm restoration of host frequency, affinity, power
profile and charging settings. A final read-only LAN download matches the
measured candidate WASM hash:

`4bdb01dab90af78db286ff529d5a25a5160cf51700550b3d3899c4193fb2a86a`.

[Complete observations, settings and artifact hashes](SWEEP_COMBINED_RESULTS.json)
are retained with the raw evidence in
`/home/claude/.scratch/eka-sweep-bottom-line/`. The historical plans and all
observations remain in `/home/claude/.scratch/eka-controlled/`; the
[checkpoint](CONTROLLED_SWEEP_CHECKPOINT.json) records completion.

## Remaining performance question

The combined Sky Force loss needs isolation. A useful next comparison is
feature mask 224 versus 160 on the same current binary, holding bound 32 and
all other settings fixed, to separate tail-prefix behavior from differences
between linked binaries. It has not been run as part of this completed sweep.
No new timing campaign is queued. Historical forced-lookup inlining and old
span-cache reuse remain uncertain leads, not additional adopted winners.
