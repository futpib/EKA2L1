# Forward-only leaf expansion on the current runtime

Status: correctness validation at the restored 74/109 historical review boundary.
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
