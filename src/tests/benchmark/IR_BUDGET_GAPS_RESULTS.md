# Budget proofs in gaps between IR segments

Policy 16 retains conditional-value IR policy 13, then places original-emitter
budget chunks only in instructions not covered by an IR segment. Segments are
selected first; chunks cannot begin inside or cross one, including its cold
fallback. Original policy 7 and earlier IR policies are unchanged.

Native and WASM builds and frontend configuration checks pass. At this checkpoint
the archived 164-case compiler suite, explicit-policy 13/16 fault matrices, and
native image/audio replays are running. No completed correctness or timing claim
is made yet. This candidate is preserved while the user's requested region-exit
census and independent execution-limit investigation take priority. No deployment.

Artifact: /home/claude/.scratch/eka-benchmark/ir-budget-candidate, with exact
source patch, source base, binary hashes and archived tests. Adversarial tests
include chunks before and after IR, taken/untaken forward joins, short budgets,
permissions, endian modes, aliases, and memory faults. All artifacts and failures
will be retained. No performance measurement has been started.

## Preserved correctness result

The archived policy-13 and policy-16 probes pass 27,712 exact native fault
comparisons, including explicit policy markers. Both checked 1,600-image
replays match merged native state/images/audio (4,656,051 stereo PCM frames),
and the candidate longer-route replay matches all 360 images and audio.

The initial compiler suite finished 163 pass / 1 fail: the added gap fixture
incorrectly assumed LDM/STM would not enter IR. Its coverage assertion rejected
the test. Replacing that fixture with conditional memory forces actual chunks
on both sides of IR and passes 7,680 adversarial comparisons. The complete
subsequent suite with exit instrumentation passes all 165 tests. The failed
log and final evidence are preserved in REGION_EXIT_CENSUS_EVIDENCE.json.
No IR timing or deployment was performed; the user-requested boundary work
remains the priority.
