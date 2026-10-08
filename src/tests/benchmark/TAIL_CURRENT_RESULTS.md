# Tail prefixes on the graduated branch-veneer runtime

Status: focused correctness passes; current-runtime timing is running. The historical sweep is paused.
The deployed default remains branch-veneer commit `0b890a26a`.

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

Both-game runtime timing is now running; no current-runtime gain is claimed yet.
