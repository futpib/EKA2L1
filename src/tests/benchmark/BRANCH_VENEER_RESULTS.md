# Single-branch callee fusion

This opt-in original-emitter experiment targets the unconditional B veneers measured in PRESERVE_INNER_RESULTS.md. Feature bit 32 executes the caller BL and the callee's single B in one compiled invocation, keeping guest values in locals. Both instructions consume exact budgets, LR retains the caller continuation, and the B takes a precise exit to its actual target. The target remains subject to normal runner validation. Only the four veneer bytes are added as a code dependency. No destination code is assumed or skipped.

Prefix features and broader eligibility remain disabled for this comparison. Source, leaf, site and runner limits remain 512/16/8/512; guest scheduling is unchanged. Additional dependency work and code size require measurement, so eligibility alone is not a performance result.

Initial validation passes 12,800 exact focused budget/state/alias/path comparisons across both TLB indices, 570 native assertions in 33 cases, and frontend policy/readback controls. Full compiler, explicit-policy native fault and native image/audio replay gates are running. No timing or deployment claim.

The immutable candidate is `/home/claude/.scratch/eka-benchmark/branch-veneer-candidate`; its source patch and artifact hashes are preserved in `source.json` alongside the binaries. All failures and samples will be retained. The live conditional-only build is unchanged.
