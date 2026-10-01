# Entry-proof fallback census after expanded leaf eligibility

Diagnostic correctness acceptance and all six census runs are complete. This is a diagnostic extension; the expanded-leaf timing archive is unchanged. No new compiler eligibility, scheduling or default policy is selected.

The preceding census classified post-store code-write exits. It did not count a write-span proof rejected at region entry, before any guest effect. Such rejection calls the precise private compiled fallback; it is not itself a browser yield or a guest-scheduler return. A false conditional store, for example, can reject an entry proof without later executing a store or triggering a post-store guard.

The new diagnostic records attempts, read/write spans checked, private fallbacks, and each valid non-wrapping write span that intersects the combined protected interval. After the compiled invocation returns, those spans are compared with the exact primary and dependency snapshots. Up to 32 spans are retained per invocation, with explicit overflow accounting. Missing mappings, permission/alignment failures, wrapping spans and pre-existing exit/budget failures are tracked separately from interval overlap. The census labels a fallback `interval_gaps_only` only when every captured overlap is a gap and no other entry-proof guard failed. This identifies a sole entry-proof cause; it does not establish how much execution time removing that cause would save.

Generated counter stores exist only with the explicit exit-census mode enabled. They preserve the guard expressions and use reserved scratch locals. Focused fixtures compare all state words, returned counts and memory with uninstrumented versions at partial budgets, using primary/dependency/gap/outside destinations and missing/read-only/writable mappings. Broader fault and native image/audio checks must pass before using these counts.

The paired census compares conditional-only leaves at length 32, expanded leaves at 32, and expanded leaves at 16 on both current post-merge routes. It retains exact instruction endpoints, presentations, rejected instructions, binding limits, dependency coverage, footprint and all raw counters. Diagnostic elapsed times will not be used as performance evidence. Timing observations and their host-contention limitation are recorded separately in `EXPANDED_LEAVES_RESULTS.md`.

## Correctness acceptance

All 170 instrumented compiler tests pass, including 97 focused rejection/guard/equivalence checks. All 33 native tests pass (548 assertions). Both explicitly selected feature modes match native on 24,512 fault cases each, 49,024 total, with census mode 1 verified. Both checked standard replays match 1,600 images, guest records and 4,656,051 stereo PCM frames; the expanded longer-route replay matches 360 images and audio. Missing, wrong and duplicate census/feature markers are rejected. The initial compile error from an incorrectly scoped diagnostic scratch name is retained in evidence, with the corrected build tested.

## Expanded-eligibility census

Six serial diagnostics use one archived executable. All modes keep conditional integer leaves enabled, source window 512 bytes, eight inlined sites, runner cap 512, original-emitter policy 7, folded TLB and grouped exact scanner. Control32 has no expanded feature bits; candidate32 and candidate16 select all three bits. These are counters, not timing samples. Exact guest instruction endpoints and presentations match within each route.

### long

| Counter | Conditional only, 32 | Expanded, 32 | Expanded, 16 |
| --- | ---: | ---: | ---: |
| Compiled invocations | 139,538,632 | 134,365,974 | 138,723,117 |
| Direct-call exits | 42,795,660 | 39,887,005 | 42,075,242 |
| BX LR returns | 12,992,581 | 10,096,901 | 12,254,398 |
| Source-window ends | 3,746,043 | 3,746,540 | 3,747,580 |
| Zero-progress invocations | 1,143,752 | 1,143,357 | 1,143,543 |
| Inlined-site limit exits | 551,884 | 586,736 | 585,954 |
| Leaf-length limit exits | 0 | 0 | 2,187,920 |
| Primary snapshot bytes requested | 9,656,322,860 | 9,372,688,048 | 9,528,295,824 |
| Dependency spans requested | 36,373,856 | 38,986,216 | 36,371,656 |
| Dependency snapshot bytes requested | 849,932,400 | 1,002,116,860 | 800,572,828 |
| Entry-proof attempts | 16,685,804 | 16,752,840 | 17,580,743 |
| Read spans checked | 20,286,123 | 20,611,367 | 20,666,723 |
| Write spans checked | 18,807,382 | 18,998,508 | 19,849,979 |
| Private entry-proof fallbacks | 715,854 | 715,762 | 733,229 |
| Fallbacks caused solely by interval gaps | 0 | 0 | 0 |

At the same 32-instruction bound, expanded eligibility changes compiled invocations by -5,172,658 (-3.71%). These are entry/return boundaries, not a count of individual state stores or loads.

control32: entry fallback causes `{"other_guard": 715854}`; overlap spans `{}`; post-store guard outcomes `{}`.

candidate32: entry fallback causes `{"other_guard": 715762}`; overlap spans `{}`; post-store guard outcomes `{}`.

candidate16: entry fallback causes `{"other_guard": 733229}`; overlap spans `{}`; post-store guard outcomes `{}`.

Remaining expanded-32 restrictions:

- `block_transfer`: 22,057,100.
- `pc_rn_or_rd_field`: 9,808,787.
- `internal_branch`: 5,868,270.
- `sp_rn_or_rd_field`: 642,571.
- `conditional_transfer`: 925.
- `unrecorded`: 762.

Leading expanded-32 rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 5,132,773 |
| `0x70062eb0` | `push {r4, lr}` | 4,894,852 |
| `0x70003c88` | `b #0x7006370c` | 4,870,471 |
| `0x70064c9c` | `push {r4, lr}` | 2,642,233 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,556,464 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,483,927 |
| `0x700653e0` | `push {r4, lr}` | 2,225,670 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,225,604 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,330,989 |
| `0x70003c78` | `b #0x70063394` | 901,809 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 885,847 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 654,641 |
| `0x70062a78` | `ldr pc, [pc, #-4]` | 577,537 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 577,520 |
| `0x70004f18` | `str lr, [sp, #-4]!` | 513,759 |
| `0x7006484c` | `bxeq lr` | 398,244 |

### standard

| Counter | Conditional only, 32 | Expanded, 32 | Expanded, 16 |
| --- | ---: | ---: | ---: |
| Compiled invocations | 139,113,870 | 134,019,919 | 138,192,187 |
| Direct-call exits | 43,743,637 | 40,852,279 | 42,958,273 |
| BX LR returns | 13,101,419 | 10,239,176 | 12,296,208 |
| Source-window ends | 3,915,493 | 3,916,394 | 3,916,403 |
| Zero-progress invocations | 1,139,100 | 1,139,516 | 1,139,377 |
| Inlined-site limit exits | 571,492 | 613,954 | 613,219 |
| Leaf-length limit exits | 0 | 0 | 2,104,928 |
| Primary snapshot bytes requested | 9,663,920,438 | 9,385,798,792 | 9,533,104,940 |
| Dependency spans requested | 36,897,258 | 39,549,726 | 36,971,821 |
| Dependency snapshot bytes requested | 864,191,580 | 1,013,489,656 | 814,486,984 |
| Entry-proof attempts | 16,707,449 | 16,774,201 | 17,640,327 |
| Read spans checked | 19,611,784 | 19,938,428 | 20,010,634 |
| Write spans checked | 18,817,563 | 19,024,258 | 19,919,191 |
| Private entry-proof fallbacks | 723,371 | 723,254 | 742,053 |
| Fallbacks caused solely by interval gaps | 0 | 0 | 0 |

At the same 32-instruction bound, expanded eligibility changes compiled invocations by -5,093,951 (-3.66%). These are entry/return boundaries, not a count of individual state stores or loads.

control32: entry fallback causes `{"other_guard": 723371}`; overlap spans `{}`; post-store guard outcomes `{}`.

candidate32: entry fallback causes `{"other_guard": 723254}`; overlap spans `{}`; post-store guard outcomes `{}`.

candidate16: entry fallback causes `{"other_guard": 742053}`; overlap spans `{}`; post-store guard outcomes `{}`.

Remaining expanded-32 restrictions:

- `block_transfer`: 22,827,614.
- `pc_rn_or_rd_field`: 9,733,386.
- `internal_branch`: 6,167,218.
- `sp_rn_or_rd_field`: 650,475.
- `unrecorded`: 762.
- `conditional_transfer`: 693.
- `predicates_disabled`: 47.

Leading expanded-32 rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 5,388,813 |
| `0x70062eb0` | `push {r4, lr}` | 5,154,119 |
| `0x70003c88` | `b #0x7006370c` | 5,126,702 |
| `0x70064c9c` | `push {r4, lr}` | 2,764,933 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,589,010 |
| `0x700653e0` | `push {r4, lr}` | 2,358,923 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,358,844 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,173,956 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,221,917 |
| `0x70003c78` | `b #0x70063394` | 941,032 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 800,633 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 680,784 |
| `0x70062a78` | `ldr pc, [pc, #-4]` | 621,749 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 621,609 |
| `0x70004f18` | `str lr, [sp, #-4]!` | 544,197 |
| `0x7002c6b8` | `push {r4, r5, r6, r7, lr}` | 408,862 |

All requested code/dependency byte and mapping checks remain. Requested byte totals are logical comparison coverage, not physical memory traffic; combined protected-interval width is not byte-validation traffic. Private entry fallback counters are distinct from outer region returns, guest scheduling and browser yielding. A gap-only label describes the sole failed entry proof, not time that would necessarily be saved by changing the guard. Footprint, compilation events, complete edge/restriction records and diagnostic elapsed times are retained in raw evidence; diagnostic times cannot establish a speedup.

## Interpretation and next boundary experiment

No primary/dependency/gap overlap is observed at entry or after stores in any of the six windows, with zero dropped overlap records. The roughly 0.72–0.74 million private fallbacks are attributed to other entry guards. This rules out interval-gap exits as a measured explanation for the mixed expanded-leaf timings on these routes; it does not justify removing code guards or extrapolating to other programs.

At the fixed 32-instruction bound, dependency-byte requests rise by 17.9% on the longer route and 17.3% on standard while primary-byte requests fall by about 2.9%. Raising expanded eligibility from 16 to 32 eliminates 2.19/2.10 million length-limit exits but increases dependency-byte requests by about 25%. The retained timings do not establish which bound is faster. These counts explain a tradeoff, not its isolated runtime cost or an optimum. Inlined-site limits still bind roughly 0.59/0.61 million times; changing them remains a separate experiment.

The raw `internal_branch` bucket still includes external tail branches. The leading example at `0x70003c88` jumps to `0x7006370c`, whose captured body saves a stack frame and exceeds 32 instructions. It cannot be treated as a short internal forward join. The 762 `unrecorded` rejections in each expanded-32 window are a remaining diagnostic-label gap: source inspection identifies the only unlabelled `callee_unsupported` return as a forward target beyond the first unconditional BX LR. The raw counts/opcodes are preserved; this structural interpretation is source-derived rather than an emitted opcode label.

A narrower next candidate is generic **callee-prefix fusion**. The captured routines at `0x70062eb0`, `0x70064c9c` and `0x70004f8c` begin with stack saves and supported scalar operations, then an unconditional nested BL after 5, 3 and 7 instructions. Together their first rejected pushes account for about 10.0/10.5 million calls in the two expanded-32 windows. That is potential coverage, not a prediction of eliminated calls or speedup: successful fusion changes subsequent compilation and entry points.

A prefix implementation can retain caller values in locals through those initial instructions and still use the existing precise exit at the nested call. It must preserve the saved caller LR, publish the nested call's real LR/PC and count, retain stack-store ordering and alias guards, and register both return continuations. It must not accidentally treat the nested call as a returning leaf or run the caller continuation immediately afterward. Initial eligibility will require a bounded straight path ending at an unconditional direct call; branches, loaded returns and unsupported operations before that call remain excluded. This does not fully fuse the nested call chain. More snapshot validation and generated code are possible costs, to be checked against a same-binary control.
