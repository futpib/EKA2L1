# Combined write-span and budget proofs: gain does not repeat

Policy 7 combines existing write-span proofs and budget chunks while retaining
original instruction lowering. It is opt-in and unserved. See
COMBINED_PROOFS_DESIGN.md for scope and exact fallback contracts.

Base 7c39b5eac plus the archived source.patch produced
`/home/claude/.scratch/eka-benchmark/combined-proofs-candidate`.
WASM SHA-256:
`7c28882fa0d33fe2945a6429a902a51aea54cc9edd4c28ddc3bd0e4f5c309c6a`.

All 149 compiler tests pass. The combined policy has 26,880 exact budget-matrix
comparisons and 16,896 write-matrix comparisons in addition to existing coverage.
Both rebuilt policies 6 and 7 pass 7,712 native fault comparisons each, including
callback remapping, exact callback state and code-write exits. Both checked
replays match native across 1,600 images, guest records and 4,919,249 stereo PCM
frames. Three native CTest targets and nine frontend checks pass. The existing
crash-repro harness XFAIL and native-identical movement-heuristic limitation
remain separate from these exact comparisons.

Captured busy functions select eight/three budget chunks, nine/eleven proved
reads and two/four proved writes. Hot bodies change 19,297 to 19,088 and 9,964 to
9,431 bytes relative to policy 6. Complete modules change 52,092 to 51,883 and
29,598 to 29,065 bytes. Captured policy-6 modules are byte-identical to the previous
archive. These static observations are not performance claims. Native probe
microtimings overlapped correctness work and are not used as performance evidence.

Serial unsampled comparisons used policies 6/7 in the same application
binary and the exact delivered read-only archive. No new live/audio acceptance
or deployment is claimed. Full source/binary hashes, raw logs and exact results
are in COMBINED_PROOFS_EVIDENCE.json. Nothing pushed.

## First serial timing batch

combined-1 12.5722s, chunks-1 13.9794s, served-1 13.3683s, served-2 14.0063s, chunks-2 13.1006s, combined-2 12.4691s.

Means: combined 12.5206s, same-binary chunks 13.5400s,
served 13.6873s. Throughput changes: +8.14% matching
control and +9.32% served. All samples retained.
Every run executes 3,975,618,624 instructions and 676 presentations with
shared audio, physical GPU and no sampling or concurrent owned heavy jobs.
The reversed confirmation follows below; no deployment.

## Reversed confirmation and decision

chunks-1 12.6891s, combined-1 14.3466s, served-1 13.3575s, served-2 13.3052s, combined-2 13.9597s, chunks-2 14.2896s.

Means: combined 14.15315s, matching chunks 13.48935s,
served 13.33135s. Throughput changes are -4.69%
against matching chunks and -5.81% against served.
Pooled means: combined 13.33690s, chunks 13.51468s, served 13.50933s.

Both first-batch candidate runs are near 12.5s; both confirmation candidates
are near 14s. All twelve runs are retained. This does not establish a repeatable
gameplay improvement; keep policy 7 opt-in and do not deploy it. No new live/audio
acceptance was attempted. The next experiment defers counter updates inside
proved chunks while retaining exact count reconstruction at every exit.
