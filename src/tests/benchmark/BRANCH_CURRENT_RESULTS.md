# Branch veneers on the graduated forward-leaf runtime

Status: controlled two-game timing at the restored 75/109 historical review boundary.
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
