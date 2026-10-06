# Single-branch callee fusion

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The preserve-inner census recorded 4,870,478 rejections at one unconditional branch instruction and 901,809 at another in the longer route. The captured-entry audit below corrects the initial assumption: only the second is a one-instruction callee; the first follows three register moves. This experiment admits the generic one-instruction ARM AL B shape as feature bit 32 under original-emitter policy 7 and conditional-leaf mode. It does not special-case guest addresses. Prefix bits 8/16 and broader eligibility bits 1/2/4 remain off.

The caller BL and callee B both execute and consume their exact instruction budgets. LR retains the original caller return address. Guest values remain in locals across that entry boundary, then the B uses the existing precise exit to its actual target, even when that target lies inside the primary window. The target is not resolved or assumed during translation; normal dispatch/code validation follows. Only the four veneer bytes become an additional exact dependency. Caller continuation discovery, code aliases, callback-visible state and ordered effects retain their existing paths. All limits remain 512 source bytes / 16 leaf instructions / 8 sites / 512 runner regions, with no guest scheduling change.

The expected benefit is one fewer compiled entry per eligible call. Additional dependency checks, enlarged protected intervals and generated code can outweigh that saving; separate counters and measurements are required. No performance claim follows from eligibility.

## Correctness

All 172 instrumented compiler tests pass, including the existing documented harness XFAIL. The new focused test makes 12,800 exact comparisons across both TLB indices, positive/negative/self/in-window targets, conditional caller paths, partial budgets, flags, LR, memory and caller/veneer aliases. Native tests pass 570 assertions in 33 cases, including four-byte and eight-byte dependency mutation/remapping.

Explicit feature modes 0 and 32 each match 40,640 native fault cases (81,280 total). The new 5,376-case fixture per mode checks actual four-byte fusion selection followed by faulting memory instructions and precise callbacks at the branch destination. Both checked standard replays and the normal candidate replay match all 1,600 images, guest records and 4,656,051 stereo PCM frames; the 360-image longer route also matches native and 2,832,756 PCM frames. Missing, wrong and duplicate feature/census markers are rejected.

Gameplay timing and any live/audio graduation remain pending. The served conditional-only archive is unchanged.

Instrumentation separately passes another 40,640 explicitly selected native fault comparisons and exact checked standard/longer image/audio replays before any diagnostic census. These checks do not contribute timing evidence.

## Serial gameplay timing

All modes retain policy 7, conditional integer leaves and limits 512/16/8/512. The matching control uses feature 0 and the candidate feature 32 in the same frozen binary. The untouched delivered conditional-only archive is a separate baseline. Diagnostics and interpreter checking are off; guest work and scheduling are identical within each route.

| Route/batch | Matching control | Branch veneers | Untouched live archive |
| --- | ---: | ---: | ---: |
| long a | 11.2172s | 11.7393s | 11.5434s |
| long b | 12.2895s | 11.1334s | 11.3569s |
| long c | 11.6708s | 12.0110s | 11.2270s |
| standard a | 11.2713s | 11.6730s | 11.5705s |
| standard b | 11.8113s | 11.2562s | 11.2990s |
| standard c | 11.8538s | 11.1581s | 11.1697s |

Each cell averages two observations. All 36 samples remain, including slow runs. Each batch mirrors its first half. Both A batches and long B keep the candidate in the middle. After slow closing candidates in A, an ordering edit raced with the running queue: long B had already loaded the original order, while standard B loaded the amended candidate-first order before restoration. The actual per-run orders are retained below. Separately labelled adaptive batch C puts the candidate at the endpoints on both routes. This additional position check was not part of the original four-batch plan. Comparisons pair corresponding halves and are not all immediately adjacent. The two-second host observer found no overlapping watched test/profile job; it does not exclude all host activity. Startup is recorded but includes guest work and is not an isolated compilation measure.

- long a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -4.45% throughput, paired +1.90% / -10.14%; versus baseline: -1.67% throughput, paired +0.65% / -3.75%.
- long b order baseline-1, candidate-1, control-1, control-2, candidate-2, baseline-2: versus control: +10.38% throughput, paired +9.70% / +11.07%; versus baseline: +2.01% throughput, paired +1.71% / +2.31%.
- long c order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: -2.83% throughput, paired +10.01% / -14.00%; versus baseline: -6.53% throughput, paired +0.96% / -13.04%.
- standard a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -3.44% throughput, paired +1.67% / -8.05%; versus baseline: -0.88% throughput, paired +3.83% / -5.13%.
- standard b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +4.93% throughput, paired -1.08% / +11.07%; versus baseline: +0.38% throughput, paired +0.51% / +0.25%.
- standard c order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +6.23% throughput, paired +0.33% / +12.10%; versus baseline: +0.10% throughput, paired +0.18% / +0.03%.

No automatic default or delivery change follows. Separately checked diagnostics measure dispatch, dependency and guard costs; normal live/audio acceptance is required for any promotion.

## Timing decision

No promotion. Longer-route throughput versus the untouched live archive changes by -1.67%, +2.01% and -6.53% across A/B/C; standard changes by -0.88%, +0.38% and +0.10%. Matching-control signs also reverse, and several slow controls inflate apparent gains. The adaptive endpoint batches do not repair the inconsistency. All 36 observations and the ordering race remain visible. The independently checked census and module measurements explain costs; they cannot replace this timing result.

## Two-mode veneer census

Instrumentation passes 40,640 additional explicitly selected native fault comparisons, a checked standard image/audio replay and the checked candidate longer replay. The existing full 172-test suite also ran instrumented. Four serial diagnostics use the frozen archive at the same original limits: control enables only conditional integer leaves and candidate additionally enables bit 32. Diagnostic elapsed time is not acceptance timing.

### long

| Counter | Conditional only | Branch veneers |
| --- | ---: | ---: |
| Compiled invocations | 140,994,669 | 139,995,135 |
| Direct-call exits | 43,526,965 | 42,529,639 |
| BX LR returns | 13,704,889 | 13,704,273 |
| Source-window ends | 3,746,623 | 3,745,913 |
| Zero-progress invocations | 1,143,911 | 1,143,902 |
| Inlined-site limit exits | 551,863 | 551,929 |
| Leaf-length limit exits | 970,429 | 970,487 |
| Primary snapshot bytes requested | 9,690,132,630 | 9,685,494,504 |
| Dependency spans requested | 35,294,035 | 36,297,708 |
| Dependency snapshot bytes requested | 768,017,640 | 772,064,808 |
| Entry-proof attempts | 17,354,245 | 17,353,413 |
| Read spans checked | 20,261,021 | 20,262,090 |
| Write spans checked | 19,472,401 | 19,472,022 |
| Private entry-proof fallbacks | 733,238 | 733,164 |
| Fallbacks caused solely by interval gaps | 0 | 0 |

Compiled invocations change by -999,534 (-0.71%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 733238}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593753408, "compiled_functions": 14257, "free_bytes": 18484048, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4057, "call_inlined": 3939, "callee_mapping_extent": 5, "callee_unsupported": 3043, "inline_dependencies": 4057, "inline_site_limit": 103, "leaf_instruction_limit": 113, "source_window_filled": 291, "source_window_not_filled": 3766}`.

candidate16: entry fallback causes `{"other_guard": 733164}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593728080, "compiled_functions": 14259, "free_bytes": 18509376, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4057, "branch_veneer_inlined": 59, "call_inlined": 3937, "callee_mapping_extent": 5, "callee_unsupported": 3020, "inline_dependencies": 4057, "inline_site_limit": 111, "leaf_instruction_limit": 117, "source_window_filled": 290, "source_window_not_filled": 3767}`.

Remaining branch-veneer restrictions:

- `block_transfer`: 22,057,737.
- `pc_rn_or_rd_field`: 9,186,928.
- `internal_branch`: 4,870,897.
- `multiply`: 1,476,812.
- `conditional_transfer`: 1,219,153.
- `sp_rn_or_rd_field`: 642,489.
- `conditional_memory`: 631,278.

Leading branch-veneer rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 5,132,799 |
| `0x70062eb0` | `push {r4, lr}` | 4,894,836 |
| `0x70003c88` | `b #0x7006370c` | 4,870,517 |
| `0x70064c9c` | `push {r4, lr}` | 2,642,205 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,556,637 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,483,920 |
| `0x700653e0` | `push {r4, lr}` | 2,225,670 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,225,596 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,330,950 |
| `0x700641f8` | `blt #0x70064208` | 1,217,466 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 885,618 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 821,592 |
| `0x70065c04` | `mul r1, r2, r1` | 654,839 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 654,646 |
| `0x70062a78` | `ldr pc, [pc, #-4]` | 577,537 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 577,332 |

### standard

| Counter | Conditional only | Branch veneers |
| --- | ---: | ---: |
| Compiled invocations | 140,626,048 | 139,585,483 |
| Direct-call exits | 44,506,411 | 43,466,498 |
| BX LR returns | 13,843,303 | 13,842,708 |
| Source-window ends | 3,915,442 | 3,915,268 |
| Zero-progress invocations | 1,138,805 | 1,139,247 |
| Inlined-site limit exits | 571,365 | 571,417 |
| Leaf-length limit exits | 1,020,658 | 1,020,664 |
| Primary snapshot bytes requested | 9,698,087,982 | 9,693,858,134 |
| Dependency spans requested | 35,765,500 | 36,812,263 |
| Dependency snapshot bytes requested | 778,289,684 | 782,474,164 |
| Entry-proof attempts | 17,400,966 | 17,400,794 |
| Read spans checked | 19,588,182 | 19,587,414 |
| Write spans checked | 19,508,488 | 19,508,258 |
| Private entry-proof fallbacks | 741,998 | 742,040 |
| Fallbacks caused solely by interval gaps | 0 | 0 |

Compiled invocations change by -1,040,565 (-0.74%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 741998}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 594094400, "compiled_functions": 14532, "free_bytes": 18143056, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4342, "call_inlined": 4177, "callee_mapping_extent": 5, "callee_unsupported": 3260, "inline_dependencies": 4342, "inline_site_limit": 105, "leaf_instruction_limit": 124, "source_window_filled": 299, "source_window_not_filled": 4043}`.

candidate16: entry fallback causes `{"other_guard": 742040}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 594055008, "compiled_functions": 14523, "free_bytes": 18182448, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4330, "branch_veneer_inlined": 60, "call_inlined": 4301, "callee_mapping_extent": 6, "callee_unsupported": 3210, "inline_dependencies": 4330, "inline_site_limit": 123, "leaf_instruction_limit": 130, "source_window_filled": 302, "source_window_not_filled": 4028}`.

Remaining branch-veneer restrictions:

- `block_transfer`: 22,829,244.
- `pc_rn_or_rd_field`: 9,124,345.
- `internal_branch`: 5,127,090.
- `multiply`: 1,582,246.
- `conditional_transfer`: 1,085,507.
- `sp_rn_or_rd_field`: 650,535.
- `conditional_memory`: 617,385.
- `predicates_disabled`: 6.

Leading branch-veneer rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 5,388,757 |
| `0x70062eb0` | `push {r4, lr}` | 5,154,106 |
| `0x70003c88` | `b #0x7006370c` | 5,126,711 |
| `0x70064c9c` | `push {r4, lr}` | 2,764,938 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,588,945 |
| `0x700653e0` | `push {r4, lr}` | 2,358,923 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,358,844 |
| `0x70062ae8` | `ldr pc, [pc, #-4]` | 2,173,853 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,222,581 |
| `0x700641f8` | `blt #0x70064208` | 1,084,032 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 900,718 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 800,566 |
| `0x70065c04` | `mul r1, r2, r1` | 681,147 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 680,970 |
| `0x70062a90` | `ldr pc, [pc, #-4]` | 621,757 |
| `0x70062a78` | `ldr pc, [pc, #-4]` | 621,749 |

Requested byte counts describe exact comparison coverage, not physical memory traffic. Private entry-proof fallbacks differ from outer compiled returns, guest scheduling and browser yielding. Whole-emulator allocator bytes and compiled-function counts do not isolate generated-code size. Full edge/rejection/compile records, sampled guest work, footprint and diagnostic elapsed times remain in evidence; no speed conclusion is drawn from diagnostic wall time.

## Generated-module size and construction diagnostics

A separate immutable loader copy uses `instrument_modules.py --metadata-only`, with no replacements. Application WASM is byte-identical to the accepted archive; the hook observes each generated-module constructor and exports. The hooked candidate passes the 360-image native checked longer replay. Two longer-route runs then capture metadata from the CPU-profile-selected worker. These include startup and all construction through 60 guest seconds; they are not just the 42–60-second timing window. Instrumentation and CPU profiling exclude these elapsed times from throughput acceptance.

| Metric | Conditional only | Branch veneers |
| --- | ---: | ---: |
| Constructed modules | 534 | 535 |
| Generated module bytes | 62,123,036 | 61,905,682 |
| Function exports, including repeat construction | 14,257 | 14,259 |
| Summed synchronous constructor milliseconds | 146.665 | 148.835 |

Generated module bytes change by -217,354 (-0.35%). Constructor duration is an observed diagnostic cost, not total browser JIT compilation time: background optimization and first-execution compilation can occur elsewhere, and a single instrumented observation per mode does not establish a repeatable compilation-speed result. All module metadata and profile-selection evidence are retained.

## Coverage correction from captured entries

The original motivation quoted rejecting instruction addresses: roughly 4.87 million calls rejected at 0x70003c88 and 0.90 million at 0x70003c78. Those are not two one-instruction callees. The captured entry at 0x70003c7c is `mov r3,r0; mov r0,r1; mov r1,r3; b ...`; its rejecting B is at 0x70003c88. Feature 32 accepts only a B at callee entry, so it leaves that hotter four-instruction veneer rejected. The entry at 0x70003c78 is a single B and is covered. Exact per-route rejection counts and captured bytes/disassembly are retained in evidence.

This explains why the measured boundary reduction is much smaller than the combined motivating rejection counts. It also identifies a possible future generic extension: retain locals through a supported linear prefix ending in a tail branch, then take its precise exit. This is an audit finding, not an implemented feature or a speed claim. The independently planned conditional-only site-limit revisit remains next.

## Completed experiment

At fixed limits, single-branch fusion removes 999,534 / 1,040,565 compiled invocations on the longer / standard route. Requested dependency bytes rise 0.53% / 0.54%; entry-proof attempts remain nearly unchanged. No gap-only entry-proof fallback or post-store code-guard exit occurs. Generated module bytes through 60 guest seconds fall 0.35%, while module count rises by one. These explain the mechanism and costs without changing the negative timing decision. The feature stays opt-in; the conditional-only build remains served. Next is the isolated conditional-only 4/8/16 site-limit revisit, recorded separately.
