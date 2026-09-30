# Invariant write spans: retained opt-in, no deployment gain established

Policy 5 extends delivered policy 4 to selected stable word-store spans. The
implementation and safety boundaries are in INVARIANT_WRITES_DESIGN.md. It is
opt-in and has not changed the LAN build. Two serial batches favor writes over matching read emission, but do not establish a meaningful improvement over the currently served archive.

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

## Performance and decision

Physical NVIDIA renderer, shared audio, guest seconds 78–96. Every run executes
3,975,618,624 instructions and 676 presentations. Sampling/detailed counters are
off; no owned heavy jobs overlap warmup or measurement. The application archive
is identical for reads/writes; served uses the previously delivered archive.

| Batch | Writes | Reads in same binary | Served | Throughput versus reads / served |
| --- | ---: | ---: | ---: | ---: |
| A | 13.15615s | 13.87010s | 13.25120s | +5.43% / +0.72% |
| B | 13.17215s | 13.31700s | 13.18335s | +1.10% / +0.09% |

A: writes 13.0968, reads 14.2718, served 13.3116, served 13.1908,
reads 13.4684, writes 13.2155 seconds.
B: reads 13.4441, writes 13.2210, served 13.1383, served 13.2284,
writes 13.1233, reads 13.1899 seconds.

All twelve samples remain, including the slow opening read control in A.
Four of four adjacent read/write pairs favor writes. Pooled means are
13.16415s writes, 13.59355s matching reads and 13.217275s served: +3.26%
throughput against matching reads, but only +0.40% against served. The ranges
overlap. This is partial position counterbalancing, not randomization or a
statistical-significance claim. The slow control inflates the pooled same-binary
percentage; there is no fixed percentage promise.

Retain policy 5 as an opt-in experiment with repeated same-binary evidence.
Do not replace LAN on this result: the advantage over the exact delivered
archive is too small to establish a useful new delivery gain. No new live/audio
acceptance run or deployment is claimed. Nothing pushed. The ongoing optimization
request continues with chunk-level budget proofs using the original emitter.

Full provenance and raw results: INVARIANT_WRITES_EVIDENCE.json. Timing commands
are serial_variants.py with reads/writes selecting policies 4/5 from the validated
candidate, served selecting policy 4 from invariant-reads-candidate, eager
regions disabled for every variant. Reproduce both orders listed above.
