# Invariant write spans: initial timing favorable, confirmation pending

Policy 5 extends delivered policy 4 to selected stable word-store spans. The
implementation and safety boundaries are in INVARIANT_WRITES_DESIGN.md. It is
opt-in and has not changed the LAN build. The first timing batch favors the candidate; confirmation is pending.

The archived candidate is based on f45aca9e0 plus its saved source.patch:
`/home/claude/.scratch/eka-benchmark/invariant-writes-validated-candidate`.
Application WASM SHA-256:
`f601f9d3dd7f71319e362b068f9586714615cce4732ccf9ccce3b7a66350477c`.

All 146 compiler tests pass, including 16,896 new write comparisons. The initial
matrix passed but only exercised false conditional stores. Review expanded its
flags to exercise both outcomes, then the full suite passed again. No compiler
code changed between those runs. The expanded archive has identical application
and fault-probe bytes to invariant-writes-candidate, which produced the fault and
replay evidence below. The known crash-repro harness XFAIL remains separate.

Policies 4 and 5 each pass all 7,712 rebuilt native fault comparisons (19 modes,
15,424 total), including 64 remapped-store cases per policy. Both checked replays
match native across 1,600 images, guest records and 4,919,249 stereo PCM frames.
All three native CTest targets and nine frontend checks pass. Replays retain the
previously documented native-identical movement-heuristic limitation; exact
comparison does not claim that heuristic passes.

In two captured busy regions, policy 5 selects 2/4 stores in addition to 9/11
reads. Body sizes change 18,780 to 18,605 and 10,862 to 10,380 bytes. Policy 4's
complete generated modules remain byte-identical to the delivered fixtures.
These are static observations, not execution coverage or speed measurements.

Next: serial unsampled gameplay comparisons with policies 4/5 in one application
binary, plus the exact served read-only archive. Every run and outlier will be
retained. Full provenance, raw log paths and exact comparisons are in
INVARIANT_WRITES_EVIDENCE.json. Nothing pushed or deployed.

Initial serial batch (writes/reads/served/served/reads/writes): writes 13.0968/13.2155s, reads 14.2718/13.4684s, served 13.3116/13.1908s. Means 13.15615/13.87010/13.25120s: +5.43% throughput versus same-binary reads and +0.72% versus served. Both adjacent pairs favor writes, but the served difference is small. All six runs execute 3,975,618,624 instructions and 676 presentations with shared audio and the physical NVIDIA renderer. No owned heavy work overlaps warmup or measurement. Every observation retained. Reordered confirmation is running; no delivery decision yet.
