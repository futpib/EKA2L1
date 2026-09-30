# Mixed IR consuming invariant read proofs

Policy 9 combines invariant read proofs with the outlined mixed IR and cold
exit recipes. Proved scalar loads consume the entry host span directly; loads
remain ordered effects, and other accesses retain their precise guards. This
does not select write proofs, budget chunks or deferred instruction counts.
The existing LAN delivery remains policy 7 from its verified archive.

## Correctness

The archive is `/home/claude/.scratch/eka-benchmark/invariant-ir-candidate`,
created from 47d1bff98 plus its archived source patch. Its application WASM
SHA-256 is `2f2305cff770157d6fd15d6ae60e12b5ae85650b3d5f2750127c90afeae2a07e`.

- All 152 compiler tests pass, including 25,600 new policy-9 interpreter
  comparisons covering budgets, register swaps, shared cold expressions,
  overwritten source memory and ordered rereads.
- Explicitly rebuilt native/WASM fault probes match in all 19 modes for both
  policies 4 and 9: 7,712 comparisons each, 15,424 total. Callback-remapping
  fixtures require actual consumption of the invariant read proof by the IR.
- Both checked native comparisons match all 1,600 images, guest records and
  4,919,249 stereo PCM frames exactly. The existing native-identical replay
  movement heuristic remains separate from exact equality.
- Three native CTest targets and nine frontend checks pass, including mode
  parsing and capability checks.

## Static coverage and cost

The two captured busy loops contain generic instruction patterns; guest
addresses are fixture identifiers only, never compiler selection criteria.

| Fixture PC | Policy | Hot body bytes | Complete module bytes | IR guards | IR proved reads |
| --- | --- | ---: | ---: | ---: | ---: |
| 0x70013edc | Previous outlined IR (3) | 14,870 | 34,686 | 45 | 0 |
| 0x70013edc | Read-only original emitter (4) | 18,780 | 38,890 | 0 | 0 |
| 0x70013edc | Combined IR (9) | 13,218 | 52,926 | 36 | 9 |
| 0x70014224 | Previous outlined IR (3) | 9,441 | 20,725 | 22 | 0 |
| 0x70014224 | Read-only original emitter (4) | 10,862 | 23,201 | 0 | 0 |
| 0x70014224 | Combined IR (9) | 7,641 | 31,047 | 14 | 8 |

Policy 9 retains 15 and 8 IR segments respectively. The second fixture has 11
entry-proved reads in total; three remain in original emission. Hot functions
shrink, but complete modules grow because precise fallback code remains.
Policies 3 and 4 produce byte-identical captured modules to their earlier
archives. Native probe microtimings overlapped correctness jobs and are not
performance evidence.

## Performance status and reproduction

No gameplay speedup is established yet. Serial timing will compare policies
9, 4 and 7 within this identical application binary, plus the exact served
policy-7 archive, with shared audio and hardware GPU. Every run includes the
same warmup and 18-guest-second measurement window; no owned heavy job overlaps
either. Keep all samples and rotate order before any promotion.

Use the existing compiler/fault/replay tools with EKA2L1_AOT_IR_MODE=9 or 4 and
EKA2L1_AOT_EAGER_REGIONS=0. `serial_variants.py` accepts `--ir-mode NAME=9`.
Raw results, fixture metadata, source attribution and hashes are recorded in
INVARIANT_IR_EVIDENCE.json. This experiment is opt-in; nothing is deployed.
