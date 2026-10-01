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

## Diagnostic label clarification

The archived enum label `internal_branch` identifies a non-link ARM B rejected by the leaf validator; it does not establish that the target lies within the callee span. For example, rejected opcode `0xea017e9f` at `0x70003c88` branches to `0x7006370c`, outside that probe. Treat this raw bucket as **direct branch, target scope not yet classified**. Opcode/PC records are retained unchanged; the disassembly audit separates internal joins from external tail branches before selecting an extension. No count or eligibility decision is changed by this clarification.

## Paired census after conditional integer fusion

All four diagnostic runs use the same archived executable and explicit default limits. Within each route the guest instruction endpoints and presentation totals match the timing workloads. Counters reconcile exactly; diagnostic wall times are not performance evidence.

### long

| Counter | Conditional leaves off | On |
| --- | ---: | ---: |
| Compiled invocations | 183,299,688 | 140,994,669 |
| Direct-call exits | 65,936,330 | 43,526,965 |
| BX LR returns | 36,084,653 | 13,704,889 |
| Source-window ends | 1,265,393 | 3,746,623 |
| Zero-progress invocations | 1,142,537 | 1,143,911 |
| Inlined-site limit exits | 105,426 | 551,863 |
| Leaf-length limit exits | 970,487 | 970,429 |
| Primary snapshot bytes requested | 10,264,195,174 | 9,690,132,630 |
| Dependency spans requested | 17,020,302 | 35,294,035 |
| Dependency snapshot bytes requested | 320,466,596 | 768,017,640 |

Compiled invocations fall by 42,305,019 (23.08%). These counts measure region entry/return boundaries; they do not count individual register transfers or helper barriers. The original emitter retains registers in WASM locals across the fused call/return.

Remaining rejection filters:

- `block_transfer`: 22,057,662.
- `pc_rn_or_rd_field`: 9,187,033.
- `internal_branch`: 5,868,339.
- `multiply`: 1,476,898.
- `conditional_transfer`: 1,219,065.
- `sp_rn_or_rd_field`: 642,470.
- `conditional_memory`: 631,238.
- `predicates_disabled`: 30.

Leading specific rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 5,132,775 |
| `0x70062eb0` | `push {r4, lr}` | 4,894,852 |
| `0x70003c88` | `b #0x7006370c` | 4,870,539 |
| `0x70064c9c` | `push {r4, lr}` | 2,642,210 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,556,542 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,483,905 |
| `0x700653e0` | `push {r4, lr}` | 2,225,670 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,225,596 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,331,033 |
| `0x700641f8` | `blt #0x70064208` | 1,217,398 |
| `0x70003c78` | `b #0x70063394` | 901,809 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 885,785 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 821,620 |
| `0x70065c04` | `mul r1, r2, r1` | 654,897 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 654,596 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 577,539 |

Code-guard outcomes: {}. Requested coverage and protected-interval totals are retained in raw evidence; interval width is not comparison traffic.

### standard

| Counter | Conditional leaves off | On |
| --- | ---: | ---: |
| Compiled invocations | 184,256,681 | 140,626,048 |
| Direct-call exits | 67,629,684 | 44,506,411 |
| BX LR returns | 36,937,957 | 13,843,303 |
| Source-window ends | 1,329,114 | 3,915,442 |
| Zero-progress invocations | 1,138,913 | 1,138,805 |
| Inlined-site limit exits | 114,191 | 571,365 |
| Leaf-length limit exits | 1,020,702 | 1,020,658 |
| Primary snapshot bytes requested | 10,277,567,968 | 9,698,087,982 |
| Dependency spans requested | 17,126,536 | 35,765,500 |
| Dependency snapshot bytes requested | 322,144,212 | 778,289,684 |

Compiled invocations fall by 43,630,633 (23.68%). These counts measure region entry/return boundaries; they do not count individual register transfers or helper barriers. The original emitter retains registers in WASM locals across the fused call/return.

Remaining rejection filters:

- `block_transfer`: 22,828,613.
- `pc_rn_or_rd_field`: 9,124,603.
- `internal_branch`: 6,167,259.
- `multiply`: 1,582,524.
- `conditional_transfer`: 1,085,471.
- `sp_rn_or_rd_field`: 650,421.
- `conditional_memory`: 617,447.

Leading specific rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 5,388,764 |
| `0x70062eb0` | `push {r4, lr}` | 5,154,119 |
| `0x70003c88` | `b #0x7006370c` | 5,126,792 |
| `0x70064c9c` | `push {r4, lr}` | 2,765,003 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,588,960 |
| `0x700653e0` | `push {r4, lr}` | 2,358,923 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,358,844 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,174,069 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,222,505 |
| `0x700641f8` | `blt #0x70064208` | 1,084,020 |
| `0x70003c78` | `b #0x70063394` | 940,951 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 900,742 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 800,520 |
| `0x70065c04` | `mul r1, r2, r1` | 681,401 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 680,816 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 621,757 |

Code-guard outcomes: {}. Requested coverage and protected-interval totals are retained in raw evidence; interval width is not comparison traffic.

Neither route records a post-store code-write guard exit in either mode, so no gap-only exit is observed here. This does not establish that combined ranges are free: failed entry span proofs/private fallback work are outside this counter, and larger dependency lists measurably increase requested checks. No mapping/byte validity or code-alias check is removed.

Dynamic site-limit hits increase after eligibility expands; the previous restrictive-inliner census cannot establish optimal limits. More capable callees must be tested against matching limits before separately revisiting bounds.
