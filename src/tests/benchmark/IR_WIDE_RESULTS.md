# Wide mixed IR: no gameplay promotion

The opt-in mixed IR retains 64-bit multiply accumulators across compatible
integer and memory operations. Low/high halves needed only by intermediate
exits are reconstructed in those cold exits. The exact compiler fallback,
budget, memory, callback and code-write contracts remain. See IR_WIDE_DESIGN.md.

| Variant | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Wide mixed IR | 15.6804 / 14.9033 | 15.29185 |
| Corrected default | 14.1920 / 14.7883 | 14.49015 |
| Served ADD/ADC | 13.5965 / 15.0061 | 14.30130 |

Order: wide/served/fixed/fixed/served/wide. The candidate has 5.2% lower mean
throughput than corrected default and 6.5% lower than served in this batch.
It is not promoted. All runs and outliers remain; this small sample is not a
precise causal estimate. Warmups and measurements were serial, with no owned
build/test/profiler overlap. Hardware GPU, shared audio, guest seconds 78–96;
each run executes 3,975,618,624 instructions and 676 presentations.

All 142 compiler tests pass, including 20,384 segment and 12,296 intermediate
snapshot/effect comparisons. All 5,952 explicitly rebuilt native/WASM fault
comparisons match, including 480 new wide-accumulator callback cases. Native
CTest passes three targets and frontend seven checks. Checked replay exactly
matches 1,600 native images, guest records and 4,919,249 stereo PCM frames.
The known native-identical gameplay movement heuristic limitation is separate.
No live acceptance or deployment was attempted.

An initial new test incorrectly expected the existing leaf selector to inline
long-multiply leaves. That run failed 141/1 and remains in the evidence. The
fixture was corrected without broadening the selector, and the full suite was
rerun successfully. Tests cover wide arithmetic around supported inlined calls,
loops, every budget, overlaps and guard failures. The inactive mixed-memory
configuration retains the prior pure-integer fixtures.

The two previously captured busy region bodies remain 29,693/16,156 bytes with
17/10 segments. The 57-instruction math fixture now has a 19,145-byte complete
body and two IR segments, including original fallback code. These sizes and
broader semantics do not establish a throughput gain.

Timing archive: `/home/claude/.scratch/eka-benchmark/ir-wide-final-candidate`.
Base c5284f9e3 plus archived patch. Main WASM SHA-256:
`487c76bde1d47ca4abb21a97a951765421d2f8d0c5629226b7ec5f0135affb4d`.
Final test fixture changes left application and fault executables byte-identical
to the archive used for replay/fault/frontend acceptance. Detailed provenance,
raw timings, checks and the failed initial fixture: IR_WIDE_EVIDENCE.json.
Experimental options remain OFF by default. Nothing pushed or deployed.
