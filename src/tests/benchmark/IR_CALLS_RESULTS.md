# Mixed IR through validated inline calls

Policy 11 keeps IR values across existing validated BL/leaf/BX sequences, with
real guest PC/LR/count snapshots and a precise first-instruction fallback for
short budgets. It stays opt-in and has not changed LAN delivery. Performance
The completed serial batch does not support replacing the served archive. This measures the combined design, including reduced
fallback duplication, not cross-call value reuse in isolation.

## Correctness

Archive /home/claude/.scratch/eka-benchmark/ir-calls-candidate is built from
e151dfcda plus its recorded source patch. Application WASM SHA-256:
fef6c382a4cbf58592121c2527b6710b5b712d021e86a0296edb361c7c2387d0.

- All 154 compiler tests pass, including 1,920 new independent interpreter
  comparisons for repeated inline calls, register cycles, flags, caller PC
  reads, memory effects, code aliases and budgets 0 through 79.
- Explicitly rebuilt native/WASM probes match all 8,384 cases for each of
  policies 10 and 11, 16,768 total across 22 modes. New callee-fault records
  compare LR/PC/flags, repaired accesses and callback stops. Separate budgets
  of 1, 2 and 3 require the production runner to complete the exact request
  after the generated function returns a shorter prefix.
- Both checked native replays match all 1,600 images, guest records and
  4,919,249 stereo PCM frames. The documented native-identical movement
  heuristic remains separate from exact equality.
- Three native CTest targets and nine frontend checks pass.

The generated function may return a shorter positive prefix on a short budget.
That is part of the existing runner contract, not permission to overshoot or
skip instructions. The independent interpreter validates its returned prefix;
production-runner/native tests validate the complete requested budget.

## Static coverage

| Fixture PC | Policy | Hot body bytes | Complete module bytes | IR segments | Inline BL/BX instructions in IR |
| --- | --- | ---: | ---: | ---: | ---: |
| 0x70013edc | Flag-aware IR (10) | 13,374 | 53,626 | 16 | 0 |
| 0x70013edc | Call-joining IR (11) | 10,976 | 32,336 | 5 | 16 |
| 0x70014224 | Flag-aware IR (10) | 7,625 | 31,175 | 8 | 0 |
| 0x70014224 | Call-joining IR (11) | 6,819 | 20,638 | 5 | 8 |

Complete modules shrink approximately 40% and 34%. Dynamic memory guard counts
remain 37 and 14. Proved reads consumed inside IR number 9 in each fixture;
the second fixture still has 11 entry-proved reads in total. Each includes one
flag-setting instruction. The old policy-10 modules are byte-identical to their
previous archive. Native probe microtimings overlapped correctness work and
are not performance evidence. Fixture addresses never select optimization.

## Reproduction

Select EKA2L1_AOT_IR_MODE=11, 10 or 7 inside the same archived application,
with EKA2L1_AOT_EAGER_REGIONS=0. Fault probes add --ir-calls and --ir-calls-short
to the previous matrix. Run test_aot_wasm.js and the standard native/frontend
checks. Replays use benchmark.ts with AOT=5, VERIFY=1024, shared audio and 1,600
images, then compare.py against native. Gameplay timing uses serial_variants.py
with the exact served archive as another control. Retain all samples and allow
no owned heavy job to overlap warmup or measurement.

IR_CALLS_DESIGN.md describes the boundaries and precise fallback. Raw results,
source/binary hashes and fixture metadata are in IR_CALLS_EVIDENCE.json.

## Completed serial batch

| Run | Seconds |
| --- | ---: |
| calls-1 | 13.7958 |
| flags-1 | 13.9644 |
| combined-1 | 13.9660 |
| served-1 | 12.5627 |
| served-2 | 13.6998 |
| combined-2 | 13.7938 |
| flags-2 | 13.6881 |
| calls-2 | 14.5595 |

Mean elapsed seconds: calls 14.1776, flags 13.8262, combined 13.8799, served 13.1312.
Candidate throughput relative to controls: flags -2.48%, combined -2.10%, served -7.38%.

All eight samples are retained. This single batch does not establish a
repeatable marginal call-IR benefit or support replacing the exact served
archive. The selected policy 7 archive remains live. No owned heavy job
overlapped warmup or measurement. The IR policies stay opt-in.
