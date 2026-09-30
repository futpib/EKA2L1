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
