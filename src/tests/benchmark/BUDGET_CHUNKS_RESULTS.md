# Budget chunks: correctness passes, timing pending

Policy 6 keeps original instruction/value/memory lowering and proves sufficient
budget once for selected straight-line chunks inside regions. Short budgets use
private precise compiled fallbacks. Delivered policy 4 supplies the read-span
proofs and serves as the same-binary control. See BUDGET_CHUNKS_DESIGN.md for
boundaries and limitations. The new policy remains opt-in and unserved.

The archived candidate is based on c1d9d1be8 plus source.patch:
`/home/claude/.scratch/eka-benchmark/budget-chunks-candidate`.
WASM SHA-256:
`a77ed6a1b19901defbc14fba8c7703253af224aa0b7420b25a81470f57545153`.

All 147 compiler tests pass, including 26,880 new chunk comparisons. Policies
4 and 6 each pass all 7,712 rebuilt native fault comparisons (19 modes, 15,424
total). Both checked replays match native across 1,600 images, guest records and
4,919,249 stereo PCM frames. All three native CTest targets and nine frontend
checks pass. Existing crash-repro harness XFAIL and native-identical replay
movement-heuristic limitation remain separate from these exact comparisons.

The two captured busy functions select eight and three chunks. Their bodies
change 18,780 to 19,297 and 10,862 to 9,964 bytes; complete modules grow to 52,092
and 29,598 bytes because of private fallbacks. The read-only control's complete
modules remain byte-identical to the delivered fixtures. These are static
observations, not dynamic coverage or performance gains.

Next: serial unsampled gameplay comparing chunk/read policies within this
application binary plus the exact served archive, retaining all samples. No
speedup, new live/audio acceptance or deployment is claimed. Full provenance,
raw log paths and exact comparisons: BUDGET_CHUNKS_EVIDENCE.json. Nothing pushed.
