# Branch veneers on the graduated forward-leaf runtime

Status: correctness validation at the restored 75/109 historical review boundary.
The live default remains graduated commit `21c322713`.

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
