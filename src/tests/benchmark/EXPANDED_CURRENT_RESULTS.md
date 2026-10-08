# Forward-only leaf expansion on the current runtime

Status: controlled two-game timing at the restored 74/109 historical review boundary.
No runtime default has changed.

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

## Completed Snakes comparison

Expanded eligibility with bound 32 improves current-runtime Snakes CPU throughput
1.10%, wall throughput 1.08%, and reduces native instructions
2.04%. All four pairs are faster, ranging from +0.79% to +1.79%. All eight
observations pass the frozen validity rules. Sky Force is still running, so no
default or LAN artifact changes yet.
