# Follow-up boundary diagnostics

User steering: finish conditional-leaf measurements, then repeat the census with specific callee rejection reasons, audit emitter-supported instructions and internal branches, measure dependency-range costs, and revisit limits after expanding eligibility. The current archived timing binaries are unchanged.

The verified diagnostics preserve the inliner's decisions. Each emitted rejected call carries its first rejecting filter, rejected guest PC and opcode. Runtime counters include address space and exact caller/rejection sites. Filter labels describe the current validator's tests, not a complete ARM operand decode; raw high register fields can have different meanings in special encodings. Instruction support must be audited from the opcode and the emitter before choosing an extension.

Code-guard diagnostics capture the backing address and width of guarded scalar/block stores and compare them with the current cache entry's primary/dependency snapshot intervals. `snapshot_overlap` overlaps at least one snapshot; `interval_gap_only` hits the combined guard while missing every snapshot. `unavailable_entry`, `multiple_guards` and `uncaptured_guard` retain incomplete coverage explicitly. Existing guards, memory effects, exit decisions and code validation are untouched. This counts conservative post-store exits, not all failed entry span proofs.

Tests cover twenty-one rejection filters/opcodes, exact dynamic counter increments, primary/dependency/gap/outside store guards, and native interval edge cases. The diagnostic archive was built after all 24 conditional-leaf timing trials completed and were committed as a8c0f5531. It passes the acceptance gates below; four serial census runs are now collecting results. Diagnostic elapsed times are not speed evidence.

After measurement, inspect the hottest remaining restrictions. Emitter support alone is insufficient for fusion: branches need internal control-flow paths and joins, actual return-target validity, exact budgets and callback snapshots. More permissive eligibility may make size/site bounds bind more often; repeat their census and controlled sweep then. No scheduling change or default enablement is proposed by this instrumentation.

Validated cache-backed invocations also accumulate primary snapshot bytes, dependency span count/bytes and the width of the combined protected interval. These are requested coverage totals for accepted entries, not hardware bytes read or isolated CPU costs; immutable startup exports without a cache entry are excluded. Protected interval width is not compared byte-for-byte and must not be interpreted as validation traffic.

## Branch-fusion audit to apply after the census

The current inlined instruction records reuse their caller's source offset while carrying the real callee guest address. All leaf instructions are excluded from primary forward-label closing and direct-loop discovery, and the final unconditional BX LR is represented by lexical continuation. Consequently, accepting B instructions in `resolve_leaf` alone would be incorrect. A branch-capable extension must assign call-site-specific internal labels, distinguish callee return paths from caller labels, and make every taken and fallthrough path agree on cached guest values, flags, PC/LR and instruction counts. Budget chunks must terminate at joins or carry a path-valid budget proof; a lexical chunk length is insufficient for a skipped path. Callback-visible snapshots and dependency validity still apply. The current eligibility extension does not implement this; the next shape must be selected from the retained census.

## Verified acceptance

- 169 instrumented compiler tests, zero failures; the existing documented harness XFAIL remains.
- 25 focused rejection-label/counter and primary/dependency/gap/outside-guard cases.
- 33 native tests with 548 assertions.
- 38,272 exact native fault matches: 19,136 each with fusion disabled/enabled, policy 7, folded TLB, grouped scanner, default 512/16/8/512 limits, and explicitly verified exit census 1.
- Both interpreter-checked standard replays match native for 1,600 images, guest records and 4,656,051 stereo PCM frames; the enabled 360-image longer route also matches.
- Missing, wrong and duplicate instrumentation markers are rejected.

The first build failed on an unqualified namespace in the new focused test. It produced no accepted archive; the corrected build passes. The failed log is retained in BOUNDARY_DETAILS_EVIDENCE.json alongside accepted checks, archived hashes and source patch. This is diagnostic acceptance, not a new timing or deployment claim. Live Snakes remains unchanged.
