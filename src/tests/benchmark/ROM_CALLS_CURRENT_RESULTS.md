# Bounded Thumb ROM calls on the current runtime

Status: not selected on the current runtime. The deployed default remains
`7f5f85273`; candidate source was restored, and its patch and artifacts retained.

Historical result: Snakes +0.17% CPU throughput (2/4 faster pairs), Sky Force
+0.61% (3/4), with native instructions +0.01% and -0.32% respectively.
That small Sky Force signal prompted this reassessment; it does not establish
an additional gain on the current runtime.

The candidate links callback-free Thumb ROM prefixes to original unlinked base
functions in the same module. The originals remain unlinked, bounding host call
depth even for guest self-calls. RAM, wrong-mode targets and prefixes containing
possible memory callbacks are excluded. Budget, stop and IRQ boundaries remain.
The newer compiled-SVC protocol propagates zero to the outer runner and includes
the caller prefix in the pending instruction count. No runtime option is added:
the frozen control and candidate artifacts isolate this code change.

Validation passed: 40,824 full-state, memory, helper-call, budget, stop, IRQ,
self-call and SVC comparisons across TLB/direct modes and page boundaries;
existing SVC and entry-budget checks; launcher policy and real browser API tests;
and both 60-frame native-reference replays with exact image/frame/PCM matches.
The actual ROM module contains 313 linked calls and 5,614 base targets.

Control: `/home/claude/.scratch/eka-tail-current/candidate-build`.
Candidate and frozen evidence: `/home/claude/.scratch/eka-rom-calls-current`.
Both retain mask 224 and limits `512,32,8,512` (primary window in bytes).
Timing uses ABBA/BAAB, measured fixed frequency, worker isolation and hardware
counter validity checks. No temperature ceiling or cooldown is used.

## Completed current-runtime comparison

| # | Game | CPU throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: |
| 1 | Snakes | -1.45% | -0.00% | 1/4 |
| 2 | Sky Force | +0.26% | -0.38% | 1/4 |

Current-runtime reassessment over 7f5f85273: Snakes -1.45% CPU throughput, Sky Force +0.26%; only 1/4 pairs faster in each game. Sky Force native instructions fall 0.38%, but three of four timing pairs lose and the small positive mean comes from one pair. This does not establish a repeatable Sky Force gain worth the measured Snakes loss. All 16 observations and 88 restoration checks pass. Retain the current deployed runtime; preserve the candidate patch and artifacts.

All 16 observations pass validity checks and all 88 restoration checks pass.
No runtime default or LAN artifact changed. The rejected patch is retained in
`eka-rom-calls-current/rejected-source.patch`. The historical sweep resumes.
