# Combined write-span and budget proofs: acceptance passed, timing pending

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

Next: serial unsampled comparisons between policies 6/7 in the same application
binary and the exact delivered read-only archive. No new live/audio acceptance
or deployment is claimed. Full source/binary hashes, raw logs and exact results
are in COMBINED_PROOFS_EVIDENCE.json. Nothing pushed.
