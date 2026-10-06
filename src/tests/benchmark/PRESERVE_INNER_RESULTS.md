# Preserve existing inner leaf fusion when selecting prefixes

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

This separate opt-in selection experiment adds feature bit 16 to prefix bit 8 (mode 24). Defaults remain unchanged. It declines an outer prefix if its first nested BL targets a returning leaf already eligible under the selected integer/memory/branch features. The existing standalone callee can then retain that inner fusion. The selected caller and callee still undergo normal exact code/mapping validation, and guest scheduling and all size/count limits remain unchanged.

The preceding prefix census motivates this rule: prefix fusion reduced direct-call exits but exposed more separate returns, slightly increasing total compiled invocations, while dependency validation grew 42–43%. Its timings were negative. This rule is a bounded profitability heuristic, not a guarantee that every remaining prefix is useful. It adds one bounded code lookup and eligibility scan during compilation; it does not recursively discover prefixes or reuse execution results.

The target is decoded from the actual BL displacement, including backward calls. The eligibility probe does not alter global feature selection. Its result only chooses a translation strategy; it is not used as a code-validity proof. A later change in nested code remains subject to the ordinary translated-region and dependency checks.

Focused tests pass 2,496 new exact selection/budget/register/flag/stack comparisons, alongside 36,960 existing prefix comparisons. They cover inactive mode 16, positive and negative BL displacements, missing mappings, unsupported/nonreturning targets, leaf length rejection, recursive-call rejection, and preservation of standalone inner fusion. A new explicit native fault fixture additionally asserts both outer-prefix selection and inner-leaf selection. Full correctness gates pass as detailed below; gameplay timing is pending. No performance or deployment claim.

The frontend initially retained one old assertion that feature value 16 was invalid; the first test run failed on that expectation. Updating it to the new invalid boundary 32 makes the policy test pass. That test-harness failure is retained; it was not an emulator failure.

## Correctness acceptance

All 171 instrumented compiler tests pass, including the existing documented harness XFAIL. The focused test passes 2,496 new selection comparisons and 36,960 existing prefix comparisons. Native tests pass 548 assertions in 33 cases. Explicit feature modes 0, 8 and 24 each match 35,264 native fault cases, 105,792 total. The new 5,376-case fixture per mode verifies preserved inner fusion as well as outer selection. Both checked 1,600-image replays and the normal candidate replay exactly match native records and 4,656,051 stereo PCM frames; the 360-image longer route also matches native. Missing, wrong and duplicate mode markers are rejected. No timing or delivery claim follows.

## Serial gameplay timing

All four modes retain policy 7 and limits 512/16/8/512. Same-binary modes use conditional-only (0), original prefixes (8), or prefixes preserving eligible inner leaves (24). The untouched delivered conditional archive is a separate baseline. Diagnostics and interpreter checking are off during timing. Guest work and scheduling are identical within each route.

| Route/batch | Conditional-only | Original prefixes | Preserve inner | Untouched delivered archive |
| --- | ---: | ---: | ---: | ---: |
| long a | 11.9385s | 11.4724s | 11.0589s | 11.6880s |
| long b | 11.2102s | 11.7110s | 11.3863s | 11.6084s |
| standard a | 11.2250s | 11.4615s | 11.8181s | 11.8161s |
| standard b | 11.9559s | 11.4593s | 11.9285s | 11.7284s |

Each cell averages two observations. All 32 samples, including slow closing runs, remain in the raw evidence. Original-prefix and candidate runs are adjacent in each half-batch; conditional-only comparisons pair corresponding positions within each half but are not immediately adjacent. Order is reversed in the confirmation batches. No watched profiling/test process overlaps according to the two-second observer; this does not exclude all host activity. Startup includes guest work and is not an isolated compilation measure.

- long a: versus control: +7.95% throughput, paired +1.80% / +14.05%; versus prefix: +3.74% throughput, paired +4.69% / +2.80%; versus baseline: +5.69% throughput, paired +7.63% / +3.77%.
- long b: versus control: -1.55% throughput, paired -2.31% / -0.75%; versus prefix: +2.85% throughput, paired +2.84% / +2.87%; versus baseline: +1.95% throughput, paired -4.12% / +8.31%.
- standard a: versus control: -5.02% throughput, paired -2.95% / -6.99%; versus prefix: -3.02% throughput, paired -0.51% / -5.40%; versus baseline: -0.02% throughput, paired +6.03% / -5.77%.
- standard b: versus control: +0.23% throughput, paired -12.96% / +15.52%; versus prefix: -3.93% throughput, paired -9.84% / +2.91%; versus baseline: -1.68% throughput, paired -12.46% / +10.82%.

No default or delivery change follows timing alone. A separately checked census follows to measure dispatch/state transfers, dependency validation and guard costs.

## Three-mode prefix census

Instrumentation passes 35,264 additional explicitly selected native fault comparisons, a checked standard image/audio replay and the checked candidate longer replay. The existing full 171-test suite also ran instrumented. Six serial diagnostics use the frozen archive at the same original limits: control enables only conditional integer leaves, prefix mode enables bit 8, and candidate enables bits 8 and 16. Diagnostic elapsed time is not acceptance timing.

### long

| Counter | Conditional only | Original prefixes | Preserve inner |
| --- | ---: | ---: | ---: |
| Compiled invocations | 140,994,669 | 141,199,203 | 135,649,062 |
| Direct-call exits | 43,526,965 | 38,173,465 | 38,181,992 |
| Exits at fused prefix nested calls | 0 | 10,910,361 | 5,341,032 |
| BX LR returns | 13,704,889 | 19,267,460 | 13,705,274 |
| Source-window ends | 3,746,623 | 3,745,746 | 3,746,434 |
| Zero-progress invocations | 1,143,911 | 1,143,719 | 1,143,652 |
| Inlined-site limit exits | 551,863 | 552,231 | 551,862 |
| Leaf-length limit exits | 970,429 | 863,146 | 863,208 |
| Primary snapshot bytes requested | 9,690,132,630 | 9,556,977,640 | 9,600,503,290 |
| Dependency spans requested | 35,294,035 | 49,415,757 | 40,770,744 |
| Dependency snapshot bytes requested | 768,017,640 | 1,096,662,044 | 861,176,496 |
| Entry-proof attempts | 17,354,245 | 14,914,156 | 17,347,848 |
| Read spans checked | 20,261,021 | 13,713,655 | 20,251,901 |
| Write spans checked | 19,472,401 | 16,505,476 | 19,463,880 |
| Private entry-proof fallbacks | 733,238 | 65,350 | 732,844 |
| Fallbacks caused solely by interval gaps | 0 | 0 | 0 |

Compiled invocations change by -5,345,607 (-3.79%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 733238}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593755480, "compiled_functions": 14257, "free_bytes": 19465144, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4057, "call_inlined": 3939, "callee_mapping_extent": 5, "callee_unsupported": 3043, "inline_dependencies": 4057, "inline_site_limit": 103, "leaf_instruction_limit": 113, "source_window_filled": 291, "source_window_not_filled": 3766}`.

prefix16: entry fallback causes `{"other_guard": 65350}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593818592, "compiled_functions": 14262, "free_bytes": 19402032, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4067, "call_inlined": 3851, "call_prefix_inlined": 647, "callee_mapping_extent": 5, "callee_unsupported": 2112, "inline_dependencies": 4067, "inline_site_limit": 145, "leaf_instruction_limit": 118, "source_window_filled": 284, "source_window_not_filled": 3783}`.

candidate16: entry fallback causes `{"other_guard": 732844}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593619592, "compiled_functions": 14236, "free_bytes": 19601032, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4036, "call_inlined": 3877, "call_prefix_inlined": 253, "callee_mapping_extent": 5, "callee_unsupported": 2713, "inline_dependencies": 4036, "inline_site_limit": 107, "leaf_instruction_limit": 121, "source_window_filled": 288, "source_window_not_filled": 3748}`.

Remaining preserve-inner restrictions:

- `block_transfer`: 13,982,892.
- `pc_rn_or_rd_field`: 6,710,582.
- `internal_branch`: 5,868,304.
- `multiply`: 1,449,400.
- `conditional_transfer`: 1,219,133.
- `sp_rn_or_rd_field`: 642,470.
- `conditional_memory`: 631,304.

Leading preserve-inner rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70003c88` | `b #0x7006370c` | 4,870,478 |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 2,674,108 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,556,635 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,483,880 |
| `0x700653e0` | `push {r4, lr}` | 2,225,670 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,225,596 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,330,984 |
| `0x700641f8` | `blt #0x70064208` | 1,217,466 |
| `0x70003c78` | `b #0x70063394` | 901,809 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 885,812 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 793,850 |
| `0x70065c04` | `mul r1, r2, r1` | 655,169 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 654,601 |
| `0x70062a78` | `ldr pc, [pc, #-4]` | 577,537 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 577,482 |
| `0x70004f18` | `str lr, [sp, #-4]!` | 513,659 |

### standard

| Counter | Conditional only | Original prefixes | Preserve inner |
| --- | ---: | ---: | ---: |
| Compiled invocations | 140,626,048 | 140,669,991 | 135,001,538 |
| Direct-call exits | 44,506,411 | 38,871,037 | 38,880,492 |
| Exits at fused prefix nested calls | 0 | 11,305,105 | 5,620,923 |
| BX LR returns | 13,843,303 | 19,521,928 | 13,844,815 |
| Source-window ends | 3,915,442 | 3,915,484 | 3,915,304 |
| Zero-progress invocations | 1,138,805 | 1,139,357 | 1,139,292 |
| Inlined-site limit exits | 571,365 | 565,185 | 571,407 |
| Leaf-length limit exits | 1,020,658 | 903,392 | 903,581 |
| Primary snapshot bytes requested | 9,698,087,982 | 9,562,806,814 | 9,604,017,074 |
| Dependency spans requested | 35,765,500 | 49,918,520 | 41,524,263 |
| Dependency snapshot bytes requested | 778,289,684 | 1,105,077,332 | 876,104,216 |
| Entry-proof attempts | 17,400,966 | 15,067,757 | 17,394,198 |
| Read spans checked | 19,588,182 | 13,206,459 | 19,576,644 |
| Write spans checked | 19,508,488 | 16,653,712 | 19,499,461 |
| Private entry-proof fallbacks | 741,998 | 83,846 | 741,679 |
| Fallbacks caused solely by interval gaps | 0 | 0 | 0 |

Compiled invocations change by -5,624,510 (-4.00%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 741998}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 594094792, "compiled_functions": 14532, "free_bytes": 19125832, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4342, "call_inlined": 4177, "callee_mapping_extent": 5, "callee_unsupported": 3260, "inline_dependencies": 4342, "inline_site_limit": 105, "leaf_instruction_limit": 124, "source_window_filled": 299, "source_window_not_filled": 4043}`.

prefix16: entry fallback causes `{"other_guard": 83846}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593964152, "compiled_functions": 14517, "free_bytes": 19256472, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4315, "call_inlined": 4114, "call_prefix_inlined": 666, "callee_mapping_extent": 5, "callee_unsupported": 2243, "inline_dependencies": 4315, "inline_site_limit": 157, "leaf_instruction_limit": 123, "source_window_filled": 294, "source_window_not_filled": 4021}`.

candidate16: entry fallback causes `{"other_guard": 741679}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593924568, "compiled_functions": 14516, "free_bytes": 19296056, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4313, "call_inlined": 4041, "call_prefix_inlined": 246, "callee_mapping_extent": 5, "callee_unsupported": 2911, "inline_dependencies": 4313, "inline_site_limit": 110, "leaf_instruction_limit": 126, "source_window_filled": 302, "source_window_not_filled": 4011}`.

Remaining preserve-inner restrictions:

- `block_transfer`: 14,336,025.
- `pc_rn_or_rd_field`: 6,522,055.
- `internal_branch`: 6,167,143.
- `multiply`: 1,548,043.
- `conditional_transfer`: 1,085,453.
- `sp_rn_or_rd_field`: 650,340.
- `conditional_memory`: 617,460.
- `predicates_disabled`: 54.

Leading preserve-inner rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70003c88` | `b #0x7006370c` | 5,126,675 |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 2,805,236 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,588,959 |
| `0x700653e0` | `push {r4, lr}` | 2,358,923 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,358,844 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,174,044 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,222,485 |
| `0x700641f8` | `blt #0x70064208` | 1,084,032 |
| `0x70003c78` | `b #0x70063394` | 940,941 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 866,295 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 800,505 |
| `0x70065c04` | `mul r1, r2, r1` | 681,367 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 680,855 |
| `0x70062a78` | `ldr pc, [pc, #-4]` | 621,749 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 621,717 |
| `0x70004f18` | `str lr, [sp, #-4]!` | 544,061 |

Requested byte counts describe exact comparison coverage, not physical memory traffic. Private entry-proof fallbacks differ from outer compiled returns, guest scheduling and browser yielding. Whole-emulator allocator bytes and compiled-function counts do not isolate generated-code size. Full edge/rejection/compile records, sampled guest work, footprint and diagnostic elapsed times remain in evidence; no speed conclusion is drawn from diagnostic wall time.

## Generated-module size and construction diagnostics

A separate immutable loader copy uses `instrument_modules.py --metadata-only`, with no replacements. Application WASM is byte-identical to the accepted archive; the hook observes each generated-module constructor and exports. The hooked candidate passes the 360-image native checked longer replay. Three longer-route runs then capture metadata from the CPU-profile-selected worker. These include startup and all construction through 60 guest seconds; they are not just the 42–60-second timing window. Instrumentation and CPU profiling exclude these elapsed times from throughput acceptance.

| Metric | Conditional only | Original prefixes | Preserve inner |
| --- | ---: | ---: | ---: |
| Constructed modules | 534 | 536 | 534 |
| Generated module bytes | 62,123,036 | 63,712,495 | 62,536,606 |
| Function exports, including repeat construction | 14,257 | 14,262 | 14,236 |
| Summed synchronous constructor milliseconds | 151.905 | 154.275 | 154.445 |

Generated module bytes change by +413,570 (+0.67%). Constructor duration is an observed diagnostic cost, not total browser JIT compilation time: background optimization and first-execution compilation can occur elsewhere, and a single instrumented observation per mode does not establish a repeatable compilation-speed result. All module metadata and profile-selection evidence are retained.

## Decision and next measured boundary

No promotion. Preserving inner fusion improves on the original prefix policy in both longer-route batch means, but loses on standard in both batches. Against conditional-only execution in the same binary, the longer-route confirmation reverses; the standard confirmation has strongly opposing pairs. The untouched live archive also remains necessary because the matching conditional-only binary differs from that archive. All 32 observations remain.

The census shows that this selection rule repairs the lost-inner-fusion mechanism, without establishing a net timing win:

- long: 5,345,607 fewer compiled invocations (3.79%), +385 additional BX-LR returns, and +12.13% dependency-byte requests. Entry-proof attempts are 17,354,245 versus 17,347,848; gap-only fallbacks are 0.
- standard: 5,624,510 fewer compiled invocations (4.00%), +1,512 additional BX-LR returns, and +12.57% dependency-byte requests. Entry-proof attempts are 17,400,966 versus 17,394,198; gap-only fallbacks are 0.

The remaining leading rejection in the longer route is a single unconditional B veneer, with 4,870,478 rejected calls; another has 901,809. A separate experiment admits only that one-instruction shape, with prefix bits disabled. It executes and counts both the original BL and veneer B, retains LR and guest locals until the normal precise target exit, and snapshots the four veneer bytes. It neither resolves nor assumes the destination code during translation. Existing target validation, budgets, interrupts/stop handling and guest scheduling remain. This is a new hypothesis, not a speed claim; any additional dependencies and code size must be measured.
