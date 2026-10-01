# Callee-prefix fusion

Implementation is opt-in and passes correctness acceptance. Serial gameplay timing does not support promotion; the feature remains opt-in. The paired census and module-size diagnostics are complete. The preceding six-run census is committed as 984133126. It found roughly 10.0/10.5 million rejected calls to three captured stack-frame routines whose first nested calls occur within seven instructions. These counts identify possible coverage; they do not predict a speedup.

Feature bit 8 admits a bounded straight callee prefix ending at an unconditional direct BL. Ordinary integer operations, scalar word/byte memory operations and non-PC block transfers use the existing original emitter. Stack saves, SP changes and saved LR values remain real guest state and memory effects. Conditional operations preserve their ordinary predicates and instruction counts. Other control transfers, loaded returns, status/coprocessor instructions and unsupported encodings before the nested call remain rejected.

The initial caller BL and prefix execute in the same generated function with guest values retained in locals. The nested BL still takes the existing precise exit and publishes its real guest PC, LR and count. The original caller continuation is not treated as fallthrough; both that return entry and the nested call's return entry are recorded. A prefix does not claim to fuse the whole nested call chain, eliminate all state publication, or change guest scheduling. Primary/dependency validation, code-alias guards and callback ordering remain required.

The independent feature is tested at the original 16-instruction leaf bound, with conditional integer leaves enabled and broader feature bits 1/2/4 disabled. A matching feature-0 control and the untouched merged archive remain comparison controls. No default is changed.

The focused suite currently passes 36,960 interpreter comparisons across all integer conditions, partial budgets, alternate caller paths, stack/data mapping misses, read-only stores and physical aliases of caller/callee code. It verifies the precise nested-call exit and absence of premature caller continuation. The new native fault fixture covers saved LR/SP and conditional flags visible at scalar fault callbacks, then continues at the nested target through the real runner. Full compiler/fault/image-audio acceptance passes as detailed below.

Diagnostics label prefix exits separately as `inlined_prefix_call`. The previously unlabelled forward target beyond the first BX LR now has the explicit structural label `forward_target_after_return`; its refusal behavior is unchanged.

## Correctness acceptance

All 171 instrumented compiler tests pass (the existing documented harness XFAIL remains), with 36,960 focused prefix comparisons also passing without instrumentation. All 33 native tests pass (548 assertions). The two explicitly selected feature modes match native for 29,888 fault cases each, 59,776 total, including saved-stack/LR and nested-call callback cases. Both checked standard replays and the normal candidate replay match 1,600 images, guest records and 4,656,051 stereo PCM frames. The checked candidate longer route matches 360 images and audio. Missing, wrong and duplicate feature/census markers are rejected. All archive hashes, source patch and accepted logs are retained in CALL_PREFIX_EVIDENCE.json. No timing or delivery claim follows from these checks.

## Serial gameplay timing

All modes keep policy 7, a 512-byte hot source window, 16-instruction leaf bound, eight sites and a 512-region runner cap. The matching control and candidate both enable conditional integer leaves; only feature bit 8 differs. The untouched merged archive is an additional baseline. These runs disable diagnostics and interpreter verification, preserve guest scheduling and retain all samples.

| Route/batch | Matching conditional-only control | Prefix fusion | Untouched merged archive |
| --- | ---: | ---: | ---: |
| long a | 11.1298s | 11.4227s | 11.8356s |
| long b | 11.1767s | 11.3741s | 11.8010s |
| standard a | 11.8015s | 12.8947s | 11.8683s |
| standard b | 11.5879s | 11.8406s | 12.0228s |

Each cell averages two observations, including slow closing runs. Matching control/candidate observations are adjacent in each half-batch. No watched profiling/test process overlapped the runs according to the two-second host observer; this does not exclude other host activity or clock variation. No sample is normalized. Startup/warmup and compiled-function/allocator counts remain in the raw reports; startup includes guest work and is not an isolated compilation measurement.

- long a: prefix versus matching control -2.56% throughput; versus archive +3.62%; adjacent matching pairs -3.30% / -1.82%; archive pairs +2.65% / +4.58%.
- long b: prefix versus matching control -1.74% throughput; versus archive +3.75%; adjacent matching pairs -1.73% / -1.74%; archive pairs +3.92% / +3.59%.
- standard a: prefix versus matching control -8.48% throughput; versus archive -7.96%; adjacent matching pairs -11.50% / -5.57%; archive pairs -6.53% / -9.33%.
- standard b: prefix versus matching control -2.13% throughput; versus archive +1.54%; adjacent matching pairs -9.57% / +5.93%; archive pairs -1.80% / +5.16%.

The paired diagnostic census below measures changed boundaries and added dependency/proof work. No default change, live/audio graduation or deployment follows timing alone.

All four batch means favor the matching conditional-only control, and seven of eight adjacent matching pairs favor that control. Prefix fusion therefore does not earn promotion. The reversed standard batch contains one faster candidate observation; it is retained alongside all slower observations. No live/audio graduation is pursued for this prefix extension on this evidence. The preceding conditional-only candidate remains a separate graduation candidate.

## Paired prefix census

Instrumentation passes 59,776 additional explicitly selected native fault comparisons, both checked standard image/audio replays and the checked candidate longer replay. The existing full 171-test suite also ran instrumented. Four serial diagnostics use the frozen archive at the same original limits: control enables only conditional integer leaves, candidate additionally enables bit 8. Diagnostic elapsed time is not acceptance timing.

### long

| Counter | Conditional only | Prefix fusion |
| --- | ---: | ---: |
| Compiled invocations | 140,994,669 | 141,199,203 |
| Direct-call exits | 43,526,965 | 38,173,465 |
| Exits at fused prefix nested calls | 0 | 10,910,361 |
| BX LR returns | 13,704,889 | 19,267,460 |
| Source-window ends | 3,746,623 | 3,745,746 |
| Zero-progress invocations | 1,143,911 | 1,143,719 |
| Inlined-site limit exits | 551,863 | 552,231 |
| Leaf-length limit exits | 970,429 | 863,146 |
| Primary snapshot bytes requested | 9,690,132,630 | 9,556,977,640 |
| Dependency spans requested | 35,294,035 | 49,415,757 |
| Dependency snapshot bytes requested | 768,017,640 | 1,096,662,044 |
| Entry-proof attempts | 17,354,245 | 14,914,156 |
| Read spans checked | 20,261,021 | 13,713,655 |
| Write spans checked | 19,472,401 | 16,505,476 |
| Private entry-proof fallbacks | 733,238 | 65,350 |
| Fallbacks caused solely by interval gaps | 0 | 0 |

Compiled invocations change by +204,534 (+0.15%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 733238}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593754960, "compiled_functions": 14257, "free_bytes": 19465664, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4057, "call_inlined": 3939, "callee_mapping_extent": 5, "callee_unsupported": 3043, "inline_dependencies": 4057, "inline_site_limit": 103, "leaf_instruction_limit": 113, "source_window_filled": 291, "source_window_not_filled": 3766}`.

candidate16: entry fallback causes `{"other_guard": 65350}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593819680, "compiled_functions": 14262, "free_bytes": 19400944, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4067, "call_inlined": 3851, "call_prefix_inlined": 647, "callee_mapping_extent": 5, "callee_unsupported": 2112, "inline_dependencies": 4067, "inline_site_limit": 145, "leaf_instruction_limit": 118, "source_window_filled": 284, "source_window_not_filled": 3783}`.

Remaining prefix-mode restrictions:

- `block_transfer`: 8,380,705.
- `pc_rn_or_rd_field`: 6,734,774.
- `internal_branch`: 5,868,374.
- `multiply`: 1,448,977.
- `conditional_transfer`: 1,219,127.
- `sp_rn_or_rd_field`: 642,477.
- `conditional_memory`: 631,305.
- `predicates_disabled`: 17.

Leading prefix-mode rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70003c88` | `b #0x7006370c` | 4,870,531 |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 2,674,006 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,583,367 |
| `0x700653e0` | `push {r4, lr}` | 2,225,670 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,225,596 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,331,034 |
| `0x700641f8` | `blt #0x70064208` | 1,217,460 |
| `0x70003c78` | `b #0x70063394` | 901,853 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 793,661 |
| `0x70065c04` | `mul r1, r2, r1` | 654,935 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 654,641 |
| `0x70062a78` | `ldr pc, [pc, #-4]` | 577,537 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 577,494 |
| `0x70004f18` | `str lr, [sp, #-4]!` | 513,666 |
| `0x70064848` | `ldreq r0, [r0, #4]` | 398,262 |
| `0x7002c6b8` | `push {r4, r5, r6, r7, lr}` | 397,918 |

### standard

| Counter | Conditional only | Prefix fusion |
| --- | ---: | ---: |
| Compiled invocations | 140,626,048 | 140,669,991 |
| Direct-call exits | 44,506,411 | 38,871,037 |
| Exits at fused prefix nested calls | 0 | 11,305,105 |
| BX LR returns | 13,843,303 | 19,521,928 |
| Source-window ends | 3,915,442 | 3,915,484 |
| Zero-progress invocations | 1,138,805 | 1,139,357 |
| Inlined-site limit exits | 571,365 | 565,185 |
| Leaf-length limit exits | 1,020,658 | 903,392 |
| Primary snapshot bytes requested | 9,698,087,982 | 9,562,806,814 |
| Dependency spans requested | 35,765,500 | 49,918,520 |
| Dependency snapshot bytes requested | 778,289,684 | 1,105,077,332 |
| Entry-proof attempts | 17,400,966 | 15,067,757 |
| Read spans checked | 19,588,182 | 13,206,459 |
| Write spans checked | 19,508,488 | 16,653,712 |
| Private entry-proof fallbacks | 741,998 | 83,846 |
| Fallbacks caused solely by interval gaps | 0 | 0 |

Compiled invocations change by +43,943 (+0.03%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 741998}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 594094792, "compiled_functions": 14532, "free_bytes": 19125832, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4342, "call_inlined": 4177, "callee_mapping_extent": 5, "callee_unsupported": 3260, "inline_dependencies": 4342, "inline_site_limit": 105, "leaf_instruction_limit": 124, "source_window_filled": 299, "source_window_not_filled": 4043}`.

candidate16: entry fallback causes `{"other_guard": 83846}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593964840, "compiled_functions": 14517, "free_bytes": 19255784, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4315, "call_inlined": 4114, "call_prefix_inlined": 666, "callee_mapping_extent": 5, "callee_unsupported": 2243, "inline_dependencies": 4315, "inline_site_limit": 157, "leaf_instruction_limit": 123, "source_window_filled": 294, "source_window_not_filled": 4021}`.

Remaining prefix-mode restrictions:

- `block_transfer`: 8,624,354.
- `pc_rn_or_rd_field`: 6,546,592.
- `internal_branch`: 6,167,147.
- `multiply`: 1,548,012.
- `conditional_transfer`: 1,085,507.
- `sp_rn_or_rd_field`: 650,437.
- `conditional_memory`: 617,203.

Leading prefix-mode rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70003c88` | `b #0x7006370c` | 5,126,723 |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 2,805,353 |
| `0x700653e0` | `push {r4, lr}` | 2,358,923 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,358,844 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,199,941 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,222,448 |
| `0x700641f8` | `blt #0x70064208` | 1,084,032 |
| `0x70003c78` | `b #0x70063394` | 940,951 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 866,292 |
| `0x70065c04` | `mul r1, r2, r1` | 681,339 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 680,762 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 621,757 |
| `0x70062a78` | `ldr pc, [pc, #-4]` | 621,749 |
| `0x70004f18` | `str lr, [sp, #-4]!` | 544,158 |
| `0x7002c6b8` | `push {r4, r5, r6, r7, lr}` | 408,896 |
| `0x70064848` | `ldreq r0, [r0, #4]` | 372,307 |

Requested byte counts describe exact comparison coverage, not physical memory traffic. Private entry-proof fallbacks differ from outer compiled returns, guest scheduling and browser yielding. Whole-emulator allocator bytes and compiled-function counts do not isolate generated-code size. Full edge/rejection/compile records, sampled guest work, footprint and diagnostic elapsed times remain in evidence; no speed conclusion is drawn from diagnostic wall time.

## Generated-module size and construction diagnostics

A separate immutable loader copy uses `instrument_modules.py --metadata-only`, with no replacements. Application WASM is byte-identical to the accepted archive; the hook observes each generated-module constructor and exports. The hooked candidate passes the 360-image native checked longer replay. Two longer-route runs then capture metadata from the CPU-profile-selected worker. These include startup and all construction through 60 guest seconds; they are not just the 42–60-second timing window. Instrumentation and CPU profiling exclude these elapsed times from throughput acceptance.

| Metric | Conditional only | Prefix fusion |
| --- | ---: | ---: |
| Constructed modules | 534 | 536 |
| Generated module bytes | 62,123,036 | 63,712,495 |
| Function exports, including repeat construction | 14,257 | 14,262 |
| Summed synchronous constructor milliseconds | 147.965 | 154.520 |

Generated module bytes change by +1,589,459 (+2.56%). Constructor duration is an observed diagnostic cost, not total browser JIT compilation time: background optimization and first-execution compilation can occur elsewhere, and a single instrumented observation per mode does not establish a repeatable compilation-speed result. All module metadata and profile-selection evidence are retained.

## Why the prefix boundary change is rejected

The paired census shows a small increase in total compiled invocations on both routes despite roughly 11 million exits from fused prefixes. Direct-call exits fall, but BX-LR returns rise by slightly more. The implementation always stops at the first nested BL; it can therefore replace an outer call boundary while giving up an existing inner returning-leaf fusion. In the longer-route samples, BX-LR returns from `0x70065af8` to `0x70004fa8` rise from 1 to 2,443 samples. Captured bytes identify `0x70065af8` as the existing six-instruction load/load/compare/conditional-MOV/conditional-MOV/BX-LR leaf, reached by the newly exposed nested call at `0x70004fa4`. These sampled edges and the source policy explain the lost inner fusion; they do not assign every timing difference to that one edge.

Dependency-byte requests rise about 42–43%, while proof attempts and checked spans decline. No entry-overlap or post-store code-guard exit appears in either mode. Thus lower private-fallback counts do not establish less memory-check work: fewer entry proofs are attempted. The current written-register analysis can also disqualify caller accesses when an added callee later writes their base, but these aggregate counters do not quantify that mechanism separately.

A next separately selectable experiment can decline prefix fusion when it would displace an already eligible returning nested leaf, preserving the existing inner fusion. It must retain precise exits and normal correctness/performance gates; this census is not a speed claim for that unimplemented selection rule.
