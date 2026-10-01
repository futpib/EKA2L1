# Integer IR segments: correct, not promoted

The first completed serial batch does not establish a gameplay gain. Keep
`EKA2L1_WASM_IR_SEGMENTS` disabled by default; no deployment or live acceptance
was attempted. The implementation is retained as opt-in research infrastructure
for further snapshot/IR work, not as an accepted optimization.

| Build | Trial seconds | Mean |
| --- | --- | ---: |
| Corrected default baseline | 13.5013 / 15.3741 | 14.43770 |
| Integer IR segments | 15.1109 / 15.4766 | 15.29375 |
| Served ADD/ADC build | 15.1367 / 14.8491 | 14.99290 |

Order: fixed/segments/served/served/segments/fixed. Mean throughput is 5.6% lower
than the corrected baseline and 2.0% lower than the served control. Ranges overlap;
these shared-host observations do not isolate the cause or establish a precise
causal regression. All samples are retained. No confirmation batch was warranted
for promotion after this result.

Every run covers guest seconds 78–96, 3,975,618,624 instructions and 676
presentations with hardware GPU and shared audio processing. Warmup and timing
were serial with no owned build/test/profile overlap. Diagnostic correctness
work ran separately and its wall times are not performance measurements.

## Implemented scope

See `IR_SEGMENTS_DESIGN.md`. Pure i32 sequences of 3–32 instructions use the
typed value graph inside existing loops and inlined leaves. The first normal
budget/exit check is retained; sufficient remaining budget selects the graph,
otherwise the original precise instructions execute. Every selected segment
uses parallel snapshot assignment. Memory, flags, control-flow and wide-value
boundaries retain their existing semantics. No guest-address selection exists.

The two captured busy regions previously excluded by whole-region IR now
contain 10 and 8 integer segments. Their emitted instruction bodies grow from
19,852 to 20,447 and from 12,083 to 13,018 bytes (excluding WASM local declarations
and the terminal end byte). Coverage of these regions is real; it did not by
itself produce a whole-game gain. The result measures value lowering together
with the segment budget guard, not an isolated CSE/DCE improvement.

## Verification and artifact

- Full WASM suite: 141 passed, including 15,680 new exact interpreter comparisons
  for copy cycles, arithmetic, wide boundaries, PC inputs, loops, branch labels,
  inlined leaves and every tested partial budget.
- All 4,944 explicitly rebuilt native fault comparisons match, including 672
  new production-runner cases requiring IR selection before a faulting access.
- Three native CTest targets and seven frontend checks pass.
- Checked replay matches 1,600 images, guest records and 4,919,249 stereo PCM
  frames exactly. The known native-identical movement heuristic remains false.

Candidate archive: `/home/claude/.scratch/eka-benchmark/ir-segments-candidate`.
WASM SHA-256:
`3f7206b0d152c9fe6a28d9ed2bac4bd17e725f92d76d089eacafdb885f36f1a0`.
Base `551b59a68` plus its archived source patch; source hashes were rechecked
before timing. `IR_SEGMENTS_EVIDENCE.json` contains provenance, comparisons,
every timing, configuration and log hashes. Reproduce with
`serial_variants.py` using the recorded archives and `EKA2L1_SHARED_AUDIO=1`.

The initial build's test process was deliberately stopped after source changed
during compilation. A complete relink and full suite followed; only the
`ir-segments-final-*` outputs above are used as acceptance evidence.
