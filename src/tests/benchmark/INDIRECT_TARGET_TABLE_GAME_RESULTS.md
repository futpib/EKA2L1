# Whole-game impact of constant-time target selection

The uncapped O(1) target-table candidate produces **no established overall
speedup** over the current eight-target shared-local fusion. This follows the
[fixed-renderer investigation](INDIRECT_TARGET_TABLE_RESULTS.md) and measures
both complete games instead of extrapolating from individual routines.

The baseline runtime is `a290e3a68`, the artifact currently served on LAN. The
candidate is the archived primary implementation with one direct fast case
followed by the perfect-hash jump table, not the eight-case-prefix alternative.
Both artifacts were frozen before measurement and their runtime/loader hashes
are recorded in the [JSON report](INDIRECT_TARGET_TABLE_GAME_RESULTS.json).

## Observed averages

Each game has four accepted runs in candidate/baseline/baseline/candidate order.
Every measurement spans guest seconds 74–92. CPU throughput change is
`mean(baseline worker CPU seconds) / mean(candidate worker CPU seconds) - 1`;
wall throughput uses wall seconds in the same formula. Positive is faster.
Unpaced realtime is eighteen guest seconds divided by mean wall time.

| # | Game | CPU throughput change | Wall throughput change | Baseline unpaced | Candidate unpaced |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes | -0.39% | +0.17% | 3.475x | 3.481x |
| 2 | Sky Force | -0.05% | -0.11% | 2.903x | 2.900x |

| # | Game / artifact | Mean worker CPU seconds | Mean wall seconds | Mean presentations | Mean retired instructions |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes baseline | 4.114447 | 5.179850 | 219.5 | 21,687,225,719 |
| 2 | Snakes candidate | 4.130475 | 5.170830 | 213.5 | 21,230,445,206 |
| 3 | Sky Force baseline | 3.575631 | 6.201135 | 562.5 | 24,612,374,075 |
| 4 | Sky Force candidate | 3.577289 | 6.207855 | 562.5 | 24,394,489,677 |

Sky Force's adjacent CPU pairs are +0.99% and -1.07%; wall pairs are -0.46%
and +0.24%. Snakes' CPU pairs are -0.24% and -0.54%; wall pairs are -0.13%
and +0.49%. These small averages are not evidence that the candidate is faster.

The work is not identical. Every Sky Force run starts at score 1,350 / stage
4% and ends at score 3,200 / stage 10%, but enemy positions and damage differ;
one candidate endpoint has one remaining life instead of two. Snakes stays in
level gameplay at score 200, but board/pickup positions and presentation counts
differ. Its mean presentations per wall second fall from 42.376 to 41.289
(-2.56%); per worker CPU second they fall 3.11%. Thus even the small positive
Snakes guest-time wall change does not demonstrate a rendering improvement.
The retired-instruction reductions (-2.11% Snakes, -0.89% Sky Force) also cannot
be treated as identical-work savings.

**Decision: keep the candidate archived.** No runtime, default, or LAN artifact
changes are made. Eight remains an implementation cap in the retained version,
not a demonstrated optimum; these specific uncapped layouts have not earned
adoption.

## Controls and verification

Chrome 153, NVIDIA Vulkan rendering, diagnostics-free/count-free runner,
hardware user-cycle/reference-cycle/instruction counters, requested 2.4 GHz.
All final observations pass the predeclared frequency and counter-running
checks; measured clocks are approximately 2394.3 MHz. Capture mode 1 retains
frame readback but omits PNG encoding, matching the earlier comparison method.
These are benchmark rates at the controlled clock, not maximum-clock LAN rates.

Sky Force runs on CPU 7 with CPU 15 reserved. After kernel-only activity on
reserved siblings invalidated Snakes attempts on CPU 7/15 and CPU 6/14, the
entire final Snakes panel uses CPU 6 with sibling CPU 14 temporarily offline.
Every measured clock snapshot verifies that CPU 14 is absent. CPU 14 is brought
back online before frequency-policy restoration, including when the idle runner
was stopped to let an unrelated build finish. Both offline-control records and
all frequency/isolation invocations confirm successful restoration.

Both endpoints of every accepted run were visually reviewed before proceeding.
The harness now captures the paused starting scene before counters start,
temporarily closing and then restoring the launcher dialog for that screenshot.
The original end screenshot remains outside timing. Final input files use
600 ms confirmation holds; Snakes spreads these every four seconds through
guest second 62 so menu navigation completes before measurement. The exact
input text, hashes, plans, eight observations, screenshot hashes and reviews
are included in the JSON report.

The readiness guard now tracks actual Cargo compiler/linker children, avoiding
an indefinite wait on a `cargo test` manager after its compilation has ended.
The existing eleven controlled-comparison tests pass. The modified profile
harness is exercised by both complete four-run gameplay panels.

## Preserved exclusions

The report retains setup and host failures separately; none enter the means:

- A candidate snapshot initially lacked launcher assets and never started the
  runtime. The complete snapshot retains the originally measured WASM/loader.
  Startup HTTP errors now fail promptly instead of timing out after two minutes.
- The original Sky Force input route left the candidate at ship selection;
  neither that menu run nor its unpaired initial baseline is used.
- The initial shorter Snakes route remained at Start Game and is excluded.
- Three gameplay attempts exceeded the existing 2% sibling-busy limit: 5.93%
  on CPU 15 for Sky Force, and 3.04% / 3.30% on CPUs 15 / 14 for Snakes. The
  latter two had zero added user ticks on the sibling; the activity was kernel
  system time, which user-process affinity alone did not remove.

Raw artifacts and reproduction drivers are under
`/home/claude/.scratch/eka-target-table-games-20261010`.
Final Sky Force observations are in `gameplay`; final Snakes observations are
in `gameplay/snakes-core6/offline-sibling`. The latter driver's `finally` block
restores the sibling before the outer frequency wrapper restores host policy.
