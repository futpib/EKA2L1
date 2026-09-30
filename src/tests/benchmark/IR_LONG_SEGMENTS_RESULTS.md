# Longer mixed-IR segments: correctness accepted, timing pending

Opt-in policy14 preserves policy13 semantics and raises only the bounded IR
segment cap32 to128. Original-emitter budget chunks remain capped32. See
IR_LONG_SEGMENTS_DESIGN.md. No game-address specializations are introduced.

All159 compiler tests pass, including34,560 new long-sequence state/flags/memory/
budget comparisons and57,344 condition comparisons repeated underpolicy14.
Both13/14 interpreter-checked replays match native for1600 images, guest records
and4,919,249 stereo PCM frames. Three native targets and nine frontend checks pass.

The standalone fault-policy audit found and corrected a test-selection error:
host Node environment variables did not reach C getenv. New explicit arguments
and required policy markers establish41,472 native matches:13,760 underpolicy7
and13,856 each under13/14. See FAULT_POLICY_AUDIT_RESULTS.md for the correction to
older reports. Tested cases are evidence, not proof of universal correctness.

| Fixture | Policy | Segments | Longest | Selected instructions | Main body bytes including local declarations | Module bytes | i32 locals |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0x70013edc | 13 | 5 | 32 | 121 | 10650 | 32004 | 82 |
| 0x70013edc | 14 | 3 | 85 | 121 | 10392 | 31190 | 136 |
| 0x70014224 | 13 | 5 | 32 | 112 | 6163 | 19976 | 86 |
| 0x70014224 | 14 | 2 | 108 | 112 | 5870 | 19157 | 177 |

All four have one i64 local. Fewer segment boundaries are confirmed, but higher
local counts may be costly. These fixture sizes do not establish throughput.

Archive: /home/claude/.scratch/eka-benchmark/ir-long-candidate-v3.
WASM SHA256:256f53fdee8c1d45bf418bbdf7e2b02546560a25458a7ed0e485add31bf9c71a.
Source:6415802e0 plus archived patch. Earlier archive application/full-test
hashes are identical; changed probe artifacts and failed assertions are retained.
IR_LONG_SEGMENTS_EVIDENCE.json records provenance and all current acceptance data.

Serial timing compares14,13,7 in one identical application and the exact served
policy7 archive. No owned heavy workload may overlap timing or warmup. Every
observation is retained. Experimental IR remains opt-in; no deployment or push.
