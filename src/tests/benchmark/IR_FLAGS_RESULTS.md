# Flag-aware mixed IR

Policy 10 extends the mixed IR with unconditional data-processing flag values.
Flags participate in liveness and precise snapshots; computations needed only
by a fault exit use the existing cold reconstruction. This remains opt-in and
has not changed the LAN delivery. Performance timing is pending.

## Acceptance

Archive: /home/claude/.scratch/eka-benchmark/ir-flags-candidate-v2, from
f4aaab4e6 plus its source patch. Application WASM SHA-256:
bcbe9633fabf41edc492de2dd282f9a4546c3188e345f52e1ddcb03a319fd39e.

- All 153 compiler tests pass, including 221,184 new exact flag/state/memory/
  budget comparisons across all 16 data-processing opcodes, 16 operand forms,
  boundary inputs, carry consumers, destination overlap and mapping failures.
- Explicitly rebuilt native/WASM fault probes match all 8,192 cases for each
  of policies 9 and 10: 16,384 total across 20 modes. The new fixture checks
  callback-visible ADDS/ADCS state before a fault and subsequent flag overwrite.
- Both checked native replays match 1,600 images, guest records and 4,919,249
  stereo PCM frames exactly. The existing native-identical movement heuristic
  remains separate from exact equality.
- Three native CTest targets and nine frontend checks pass. Mode 10 is parsed
  as a whole value, with compile-time capability checks preserved.

The new seven-instruction fault fixture initially retained the old five-
instruction generated-count range. That harness assertion was corrected to
3..7 and both probes explicitly rebuilt. The original failed logs remain.
Application, full compiler-suite and compiler-probe binaries are byte-identical
between the initial and corrected archives, so their completed results apply
to the same executable bytes. The fault matrix above uses the rebuilt probes.

## Static fixtures

| PC | Policy | Hot body bytes | Complete module bytes | IR segments | Flag instructions in IR |
| --- | --- | ---: | ---: | ---: | ---: |
| 0x70013edc | Previous IR (9) | 13,218 | 52,926 | 15 | 0 |
| 0x70013edc | Flag-aware IR (10) | 13,374 | 53,626 | 16 | 1 |
| 0x70014224 | Previous IR (9) | 7,641 | 31,047 | 8 | 0 |
| 0x70014224 | Flag-aware IR (10) | 7,625 | 31,175 | 8 | 1 |

Only one flag-setting instruction in each captured busy loop joins the IR.
This is modest additional coverage; calls, conditions, branches and the segment
length bound remain. The previous-policy fixture modules are byte-identical to
their earlier archive. Addresses are fixture identifiers, never selection rules.
Native microtimings overlapped correctness jobs and are not performance evidence.

## Reproduction

Use EKA2L1_AOT_IR_MODE=10 (candidate), 9 (previous IR), or 7 (combined original
emitter) with the same archived application, and EKA2L1_AOT_EAGER_REGIONS=0.
Fault probes additionally accept --ir-flags. Full tests use test_aot_wasm.js;
replays use benchmark.ts with AOT=5, VERIFY=1024, shared audio and 1,600 images,
then compare.py against the native archive. Gameplay comparisons use
serial_variants.py and retain every sample, including the exact served archive
as an external control. No owned heavy job may overlap warmup or measurement.

See IR_FLAGS_DESIGN.md and IR_FLAGS_EVIDENCE.json for implementation scope,
source/binary hashes, exact raw results and fixture metadata. There is no
performance or deployment claim from these correctness results.
