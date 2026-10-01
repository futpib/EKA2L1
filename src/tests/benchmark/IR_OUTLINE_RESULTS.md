# Private IR fallbacks: smaller hot functions, no promotion

Experimental IR_OUTLINE moves each segment's original short-budget path into
an unexported direct-call function. Full-budget execution retains the same IR
values, ordered memory effects and exact exit snapshots. The caller publishes
state and remaining budget before fallback, restores the original budget after
it, and returns without stale caller writeback. See IR_OUTLINE_DESIGN.md.

| Variant | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Outlined IR | 13.9904 / 13.7845 | 13.88745 |
| Preceding addressing IR | 15.3880 / 14.4076 | 14.89780 |
| Corrected default | 13.4361 / 13.5997 | 13.51790 |
| Served ADD/ADC | 13.4636 / 13.4517 | 13.45765 |

Order: outline/addressing/fixed/served/served/fixed/addressing/outline.
Both outline runs beat the preceding IR in this batch; mean throughput is
7.3% higher. However, it is 2.7% lower than corrected default and 3.1% lower
than served. This is not a promoted gameplay improvement. All eight samples
are retained, including variation in the preceding IR; the sample does not
establish a precise causal effect. Warmups and measurements were serial with
no owned build/test/profiler overlap. Hardware GPU and shared audio, guest
seconds 78–96; each run executes 3,975,618,624 instructions and 676 presentations.

All 143 compiler tests, 7,104 rebuilt native/WASM fault comparisons, three
native CTest targets and seven frontend checks pass. Checked replay exactly
matches 1,600 native images, guest records and 4,919,249 stereo PCM frames.
The existing native-identical movement heuristic still fails (408,561 us maximum
image gap and 0.00248 minimum viewport change); exact equality is separate.
Tests exercise private call relocation, multiple public/private targets,
indices beyond 127, exact restored budgets, inlined leaves, loops and faults
inside short-budget fallbacks. The new 480-case probe initially differed in
106 cases: its native Step harness continued after a callback requested stop.
The harness now honors that request; all fault modes were rebuilt and rerun.
No compiler correction was needed for that discrepancy. Initial failure
artifacts remain, and app/full-suite binary hashes stayed identical.

Two captured busy parent bodies shrink from 30,616/17,754 to 14,870/9,441 bytes,
with 15/8 segments. Complete modules including private helpers are 34,686/20,725
bytes. Smaller hot functions are not smaller complete modules and do not
establish a whole-game gain.

Archive: `/home/claude/.scratch/eka-benchmark/ir-outline-final-candidate`.
Base 584e147a4 plus archived patch; source and archive hashes were checked
before timing, and sources checked again when recording this result.
Main WASM SHA-256:
`64a7a6338922bbaac3f471c2e81f45fe006fa9374f575cf472e2a2ef5881bf23`.
IR_OUTLINE and the other IR options remain OFF by default. No live acceptance,
push or deployment. IR_OUTLINE_EVIDENCE.json records raw runs and acceptance.

Next discriminator: hold the main application JS/WASM binary fixed while
selecting original, inline-IR and outlined-IR generation before startup. The
separate archives above change that surrounding binary too. This is a possible
confound, not an established explanation of the measured results. It does not
justify discarding any of these measurements.
