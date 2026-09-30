# Fault-only reconstruction: no promotion

> Correction (2026-09-30): standalone per-policy fault coverage is superseded by
> [the explicit-policy audit](FAULT_POLICY_AUDIT_RESULTS.md). Original raw results remain below.

Policy 3 moves pure values used only by fault snapshots into those exits,
memoizing shared expressions once per exit. It retains all ordered memory
effects and saved load values. The implementation is opt-in; configured and
default behavior are unchanged. See IR_RECIPES_DESIGN.md for the contract.

The candidate passes all 144 compiler tests, including 81,536 segment cases
across enabled policies, 24,584 intermediate memory-exit cases and 6,120 new
load-history/shared-DAG/width/budget cases. Explicitly rebuilt native/WASM
probes match in all 7,584 cases for each of policies 0, 2 and 3: 22,752 total.
Native CTest passes three targets and the frontend passes eight checks.

Each of policies 0, 2 and 3 has a fresh checked replay matching native across
1,600 images, guest records and 4,919,249 stereo PCM frames. The known
native-identical movement-heuristic limitation is separate from exact equality.
These tests establish the checked cases, not general correctness of every
possible guest program.

For both captured busy loop fixtures, policies 0/1/2 emit modules byte-identical
to the previous versions. Policy 3 is also byte-identical to policy 2 for these
two functions: they contain no cold values. Synthetic tests require actual
non-half recipes. No dynamic coverage estimate or speedup follows from them.

Archive: `/home/claude/.scratch/eka-benchmark/ir-recipes-candidate`.
Source base: `73b2592350efbb56b5b7ee50e68d7c4416f9da0d` plus archived patch.
Application WASM SHA-256:
`3b0384b5ca7bdeb82a39fb46225f2a8c38dbc8778f83a46d9f3f58943399aef4`.
IR_RECIPES_EVIDENCE.json records source/binary hashes and exact comparisons.

| Policy | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Exit recipes, mode 3 | 13.8035 / 15.5415 | 14.67250 |
| Previous outlined IR, mode 2 | 15.4536 / 13.8673 | 14.66045 |
| Original emission, mode 0 | 13.4585 / 13.4184 | 13.43845 |
| Served build | 13.5546 / 14.6461 | 14.10035 |

Order: recipes/outline/plain/served/served/plain/outline/recipes. Recipes
are effectively tied with previous outlined IR and show 8.4% lower mean
throughput than original emission, 3.9% lower than the served control.
This noisy sample is not a precise causal estimate of the regression. The
successful-path optimization has not established a useful gameplay gain.
All eight observations remain, including the slow closing recipe run.

Policies 0/2/3 use identical JS/WASM application bytes. Timings are warmed,
serial, hardware GPU, shared audio, unsampled and without detailed counters,
guest seconds 78–96. Each executes 3,975,618,624 instructions and 676
presentations. No owned build, test or profiler overlaps warmup or timing.
This controls the surrounding application binary, not browser/host variation.
The served control uses its existing separate archive. No promotion, push or
deployment; policy3 and all experimental IR options remain opt-in.

Reproduction: build/archive the IR_SEGMENTS/IR_MEMORY/IR_OUTLINE-enabled
candidate with DEFER_MEMORY enabled and CODE_VERSIONS disabled. Run
`serial_variants.py ASSETS NEW_OUTPUT recipes=ARCHIVE outline=ARCHIVE
plain=ARCHIVE served=SERVED --ir-mode recipes=3 --ir-mode outline=2
--ir-mode plain=0` with EKA2L1_SHARED_AUDIO=1. Fault probe selection is
EKA2L1_AOT_IR_MODE=0/2/3; the new mode is `--ir-recipes` (480 cases).

Next coverage experiment: eagerly installed ROM exports do not reach the
miss-driven hot compilation route. Test the existing region compiler there,
opt-in and with IR disabled initially, using the same application binary for
both settings. Measure startup/code-size costs as well as warmed throughput.
