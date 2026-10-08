# Tail prefixes on the graduated branch-veneer runtime

Status: current-runtime correctness and timing pending; historical sweep paused.
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
