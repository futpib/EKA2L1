# Mixed IR consuming invariant read proofs

Policy 9 is not promoted: its completed serial batch averages 13.5747 seconds,
versus 13.4210 for read-only emission, 13.4352 for combined proofs in the same
binary, and 12.6498 for the exact served archive.

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

No gameplay speedup is established. Serial timing compared policies
9, 4 and 7 within the identical application binary, plus the exact served
policy-7 archive, with shared audio and hardware GPU. Every run includes the
same warmup and 18-guest-second measurement window; no owned heavy job overlaps
either. All samples are retained. Both adjacent IR/read-only pairs favor the
read-only control; no promotion confirmation is warranted for this candidate.

Use the existing compiler/fault/replay tools with EKA2L1_AOT_IR_MODE=9 or 4 and
EKA2L1_AOT_EAGER_REGIONS=0. `serial_variants.py` accepts `--ir-mode NAME=9`.
Raw results, fixture metadata, source attribution and hashes are recorded in
INVARIANT_IR_EVIDENCE.json. This experiment is opt-in; nothing is deployed.

## Completed serial batch

| Run | Seconds |
| --- | ---: |
| ir-1 | 13.3751 |
| reads-1 | 13.2981 |
| combined-1 | 12.5222 |
| served-1 | 12.6038 |
| served-2 | 12.6957 |
| combined-2 | 14.3481 |
| reads-2 | 13.5438 |
| ir-2 | 13.7742 |

Mean elapsed seconds: ir 13.5747, reads 13.4209, combined 13.4352, served 12.6498.
Candidate throughput relative to controls: reads -1.13%, combined -1.03%, served -6.81%.

All eight samples are retained, including the 14.3481-second combined control.
This batch does not support delivering policy 9. No claim is made that the
negative result estimates a precise causal regression. The selected policy 7
archive remains served. No owned heavy work overlapped warmup or measurement.
