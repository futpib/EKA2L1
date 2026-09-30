# Budget chunks: matching-compiler gain, mixed served comparison

> Correction (2026-09-30): standalone per-policy fault coverage is superseded by
> [the explicit-policy audit](FAULT_POLICY_AUDIT_RESULTS.md). Original raw results remain below.

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

Full provenance, raw log paths and exact comparisons are in BUDGET_CHUNKS_EVIDENCE.json. Nothing pushed or deployed.

## First timing batch

Serial order: chunks 12.6242, reads 13.2335, served 13.2804, served 14.2402,
reads 14.8659, chunks 13.9724 seconds. Means are 13.2983s chunks, 14.0497s
same-binary reads and 13.7603s served: +5.65% and +3.47% throughput. Both adjacent
read/chunk pairs favor chunks. Closing samples slow in all three variants, and
ranges overlap. Every sample is retained; this does not establish a fixed gain.
Each run executes 3,975,618,624 guest instructions and 676 presentations with
shared audio and the physical NVIDIA renderer. No owned heavy jobs overlap
warmup or measurement. Reordered confirmation follows below; no promotion.

## Reordered confirmation

Order: reads 13.3505, chunks 14.0795, served 13.2474, served 13.1591,
chunks 12.6281, reads 14.4267 seconds. Means are 13.3538s chunks, 13.8886s
matching reads and 13.20325s served: +4.00% against matching reads, but -1.13%
against served. One of two adjacent chunk/read pairs favors each policy.

Across both batches, means are 13.32605s chunks, 13.96915s matching reads and
13.481775s served: +4.83% and +1.17% throughput respectively. Three of four
adjacent chunk/read pairs favor chunks, but the served comparison changes sign.
All twelve samples are retained, including slow controls and candidates.
The candidate's four samples fall near 12.6s or 14.0s; their cause is unproven.

Decision: keep this as an opt-in experiment and do not replace the served build.
Investigate execution variability with separate hardware-counter diagnostics.
Those diagnostics will not be pooled with these ordinary timing controls, used
to subtract noise, or treated as promotion evidence. No new live/audio acceptance
has been performed for policy 6.
