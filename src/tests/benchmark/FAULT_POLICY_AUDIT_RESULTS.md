# Correction: standalone WASM fault-probe policy selection

2026-09-30. The policy-14 long-segment coverage assertion exposed that Node's
`process.env.EKA2L1_AOT_IR_MODE` was not automatically copied into Emscripten's
C environment. The old runners set the host environment and labelled outputs
with that number. The WASM probe instead saw no variable and selected
`arm_ir_policy::configured` (-1).

Consequently, per-policy fault counts from the same-binary selection work
(73b259235) through the conditional-IR reports are **configured-default results,
not evidence that each requested policy was exercised**. Counts and raw semantic
comparisons remain recorded, but those attribution/coverage claims are withdrawn.
Compile-time experiments before that selection mechanism are a separate case.

This does not invalidate direct-policy compiler test observations or browser
replays/timings: those callers respectively pass the enum into translation or
call `eka2l1_ir_configure` before initialization and reject a nonzero response.
It also does not prove general correctness of any policy. Acceptance requires
the newly verified fault runs below.

The probe now accepts `--ir-policy=N` explicitly and sets the C environment for
the production runner too. It prints `PROBE_POLICY N`. The repository matrix
runner passes that argument to both executables, and the comparator's
`--ir-policy` option rejects missing, duplicate or different policy markers.
Invalid policy15 is rejected by both executables. Deliberately missing/wrong
markers are rejected in comparison. Evidence: scratch `ir-policy-contract/`.

The real selected policies also exposed an obsolete assertion: the four-load
chain consumes four invariant entry proofs, rather than four dynamic guards.
The assertion now requires either the dynamic guards or all four IR proof
consumers for that specific fixture. It cannot pass merely because no guard
was generated. No emulator semantics changed for this correction.

The current-source audit is complete against `ir-long-candidate-v3`:

| Explicit policy | Native-matched cases | Modes |
| --- | ---: | ---: |
| 7, delivered compiler policy | 13,760 | 23 |
| 13, conditional mixed IR | 13,856 | 24 |
| 14, longer mixed IR | 13,856 | 24 |

All 41,472 cases match, including complete callback-visible state and changed
memory. Policies13/14 include the new fault-after111-instructions fixture.
The current compiler suite passes159 tests, and both13/14 browser replays match
all1600 native images/guest records and4,919,249 stereo PCM frames. Full source,
probe hashes, policy markers and comparison results are recorded in
IR_LONG_SEGMENTS_EVIDENCE.json. This revalidation applies to the current source;
it does not retroactively validate every older archive or restore its false
per-policy attribution. Older inactive experiments retain the correction above.

The first archive stopped at the new coverage assertion before claiming success.
The second archive exposed the four-proof chain assertion described above. Both
failed attempts are preserved. The third archive contains the corrected probe;
application and full-test JS/WASM hashes are identical across all three archives.
No emulator defect was established by this audit. The LAN application is unchanged.
