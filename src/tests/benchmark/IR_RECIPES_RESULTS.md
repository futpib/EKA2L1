# Fault-only reconstruction: acceptance complete, performance pending

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

Performance measurements are pending. No promotion, push or deployment.
