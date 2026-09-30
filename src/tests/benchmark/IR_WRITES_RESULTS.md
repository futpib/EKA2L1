# Invariant write proofs inside mixed IR

Policy 12 extends the preceding call/flag-aware IR with the original emitter's
entry-proved write spans. It remains opt-in; gameplay timing is pending.
The current LAN executable is unchanged. See IR_WRITES_DESIGN.md for contracts.

## Correctness

Archive `/home/claude/.scratch/eka-benchmark/ir-writes-candidate-v2` records base
263ff23ee plus the exact source patch. Application WASM SHA-256:
`6662ac5e5d4591497235bb80d1965a7b117bd69ee637c0088bc75ed8ce2a3db6`.

- All 156 compiler tests pass, including 1,920 new call/budget comparisons and
  16,896 new write-state/budget cases. These include permission failures,
  misalignment, endian state, page ends, conditional stores, pending exits,
  physical code aliases, register cycles and independent interpreter checks.
- Explicitly rebuilt probes match all 8,384 native cases for each of policies 11
  and 12, 16,768 total across 22 modes. Callback-remapping fixtures require actual
  proved IR writes and verify that later accesses use the new backing page.
- Both checked native replays match 1,600 images, guest records and 4,919,249
  stereo PCM frames. The known native-identical movement heuristic failure
  remains separate from exact equality.
- Three native CTest targets and nine frontend checks pass.

Review found a probe-only selection assertion error in the first archive:
policy 12 was incorrectly listed among IR-disabled modes. The v2 probes remove
that exemption and strengthen flag-selection coverage for 11/12. All fault
modes were rebuilt and rerun. Application/full-suite/fixture binaries are
byte-identical between archives; the first replay and suite remain attributable
to the v2 executable. Initial observations and correction provenance are kept.

## Static fixtures

| Guest PC | Previous IR body / module | Write-proof IR body / module | Dynamic guards before / after | IR proved writes |
| --- | ---: | ---: | ---: | ---: |
|0x70013edc|10,976 /32,336 bytes|10,644 /32,004 bytes|37 /35|2|
|0x70014224|6,819 /20,638 bytes|6,157 /19,976 bytes|14 /12|2|

Old policy 11 modules are byte-identical to the preceding archive. These fixture
addresses never select optimization. Translation microtimings overlapped other
correctness work and are not performance evidence.

## Reproduction

Select EKA2L1_AOT_IR_MODE=12, 11 or 7 within the same archive; eager regions 0.
Run test_aot_wasm.js, the 22-mode native fault matrix, native/frontend checks,
and benchmark.ts with AOT 5, VERIFY 1024, shared audio and 1,600 images. Compare
against the archived native replay with compare.py. Gameplay comparisons use
serial_variants.py and the exact served archive as a further control, with no
owned heavy job overlapping warmup or measurement. Retain every sample.

Raw hashes, coverage metadata and comparison outputs: IR_WRITES_EVIDENCE.json.
