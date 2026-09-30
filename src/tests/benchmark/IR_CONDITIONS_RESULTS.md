# Conditional integer IR: correctness accepted, timing pending

Policy 13 extends the opt-in mixed IR across pure conditional integer operations.
Predicates use preceding flags; false conditions still consume an instruction.
Conditional memory and control transfers remain original-emitter boundaries.
See IR_CONDITIONS_DESIGN.md for the value and precise-exit contract.

All 157 compiler tests pass, including 57,344 new condition/flag/budget/fault
comparisons. Explicitly rebuilt probes match native in all 27,520 cases across
policies 12 and 13 (13,760 each), including 5,376 new conditional cases per policy.
Both interpreter-checked gameplay replays match native for all 1,600 images,
guest records and 4,919,249 stereo PCM frames. Three native targets and all nine
frontend checks pass. These matrices support the tested cases, not a proof of
universal correctness.

The two previously captured busy-loop fixtures are byte-identical under policies
12 and 13, with zero conditional IR instructions. Any measured whole-game change
must arise elsewhere; this extension does not improve those two fixture bodies.

Archive: /home/claude/.scratch/eka-benchmark/ir-conditions-candidate.
WASM SHA-256: 0c8222476afe98f92e00d6ac49551757857f478f810421fe4f2ef83c4cbeeab2.
Source is e3953b69a plus the archived patch. The initial build failed because the
select opcode constant was missing; the successful rebuilt archive includes it.
The rejected build logs remain in scratch. IR_CONDITIONS_EVIDENCE.json records
source/probe hashes, exact comparisons and fixture provenance.

Performance is pending. Compare policies 13, 12 and 7 in this same application
binary plus the exact served policy-7 archive. Retain all observations. Nothing
is deployed or pushed; the current HTTPS application remains unchanged.
