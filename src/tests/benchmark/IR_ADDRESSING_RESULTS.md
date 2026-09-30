# Broader IR memory addressing: no promotion

The mixed IR now carries values through ordinary byte/halfword accesses,
including signed loads, scaled register offsets and conservative PC-relative
loads. Width-specific aligned TLB proofs, ordered effects and precise exits
retain the original fallback. See IR_ADDRESSING_DESIGN.md for the contract.

| Variant | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Served ADD/ADC | 13.4326 / 13.5135 | 13.47305 |
| Corrected default | 14.6842 / 13.3897 | 14.03695 |
| Addressing IR | 14.0667 / 14.0408 | 14.05375 |

Order: served/fixed/addressing/addressing/fixed/served. Candidate throughput is
0.12% lower than corrected default (effectively tied) and 4.1% lower than served
in this batch. No promotion or live acceptance follows. All samples are retained;
this small sample is not a precise causal estimate. Warmups and measurements
were serial with no owned builds/tests/profilers overlapping. Hardware GPU and
shared audio, guest seconds 78–96; every run executes 3,975,618,624 instructions
and 676 presentations.

All 143 compiler tests pass, including 24,112 new exact width/address/snapshot/
budget comparisons. All 6,624 explicitly rebuilt native/WASM fault comparisons
match, including 672 new cases requiring addressing IR guards. Native CTest
passes all three targets; frontend passes seven checks. Checked replay exactly
matches 1,600 native images, guest records and 4,919,249 stereo PCM frames.
The known native-identical gameplay movement heuristic limitation is separate.

The captured busy bodies have 15/8 IR segments versus 17/10 previously. Their
complete bodies grow from 29,693/16,156 to 30,616/17,754 bytes; the new memory
forms join some previously split sequences but retain snapshots and the original
fallback. Broader coverage and repeated candidate timing do not establish a gain.

Archive: `/home/claude/.scratch/eka-benchmark/ir-addressing-candidate`.
Base 2e1d113c9 plus archived patch; source/archive hashes checked before timing,
and source hashes rechecked when recording this result. Main WASM SHA-256:
`51dad11bd34cb79bb43827a3f3692999e621950daa3f7c2defdbb713421b6f0a`.
Raw timings and acceptance provenance: IR_ADDRESSING_EVIDENCE.json.
Experimental options remain OFF by default. Nothing pushed or deployed.
