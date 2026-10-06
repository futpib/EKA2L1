# Register-only tail-prefix experiment

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

Status: opt-in implementation with focused checks; full acceptance and timings pending. No deployment.

The preceding captured code shows that the hotter branch veneer begins with three register moves. Single-instruction feature32 leaves it unfused. New feature64 accepts a nonempty, bounded register-only integer prefix followed by an unconditional ARM branch. It uses the original emitter with conditional leaves enabled and the retained 512/16/8/512 source/leaf/site/runner limits. It does not recognize game addresses.

Guest values remain in WASM locals across the call and prefix. Every operation, false predicate and terminal branch consumes its exact budget; LR keeps the caller return address. The terminal branch takes the ordinary precise exit even when its destination lies inside the caller source window. Destination validation and scheduling are unchanged. The complete prefix becomes an exact code dependency; target bytes are not assumed. SP/LR/PC operands, memory, status transfers, multiply and nested/internal control flow remain excluded. Pure single-instruction veneers retain their separate opt-in feature32.

Focused tests pass 86,400 exact interpreter comparisons across flags, predicates, register shifts, short budgets, positive/negative/self/in-window branch targets and caller stores aliasing primary/dependency bytes. Selection tests cover exclusions and length boundaries. Native CPU/cache tests pass 570 assertions in 33 cases. Native/WASM builds and browser configuration/readback checks pass. A stale frontend negative test initially rejected the newly valid mask64; that failure and correction are retained.

Full compiler and explicit native fault checks are running. Browser replay and any timing follow the currently frozen site-limit diagnostic queue. Performance is unmeasured. No claim is made that removing an entry boundary outweighs the added dependency scan or generated code.

## Compiler and focused fault gates

All 173 instrumented compiler tests pass, including the new 86,400 comparisons; the existing documented XFAIL remains. The new tail-prefix fault fixture passes 5,376 cases each in control, candidate and instrumented candidate modes (16,128 comparisons, with explicit compiler/feature/readback checks). Six missing/wrong/duplicate marker cases are rejected. These focused cases will also occur within the later full matrix; they must not be misreported as distinct coverage.

The planned timing comparison uses control/candidate/live followed by its mirror, then candidate/live/control followed by its mirror, separately on both routes: 24 total observations. All modes retain the same 512/16/8/512 limits. Only feature64 differs in the matching binary; the untouched conditional-only live archive is separate. Passive host telemetry and all samples are retained. Timings start only after full faults and exact native replays pass and all diagnostic/build jobs finish.

## Full correctness acceptance

Both control and candidate pass 46,016 explicitly selected native fault comparisons (92,032 total), including the new prefix fixture. Checked control/candidate and normal candidate standard replays match all 1,600 native images, guest records and 4,656,051 PCM frames. The checked longer route matches 360 images and 2,832,756 PCM frames. Actual compiler policy, feature mask, limits and archive hashes are verified. These full matrices include cases also reported by the focused runs; no extra distinct coverage is implied.

No new timing or live/audio acceptance is claimed yet. The previous conditional-only archive stays served.

## Serial gameplay timing

All modes retain policy 7, conditional integer leaves and limits 512/16/8/512. The matching control uses feature 0 and the candidate feature 64 in the same frozen binary. The untouched delivered conditional-only archive is a separate baseline. Diagnostics and interpreter checking are off; guest work and scheduling are identical within each route.

| Route/batch | Matching control | Tail prefixes | Untouched live archive |
| --- | ---: | ---: | ---: |
| long a | 11.1197s | 11.7619s | 11.1770s |
| long b | 11.6982s | 11.3686s | 11.8925s |
| standard a | 11.2337s | 11.0879s | 11.9243s |
| standard b | 12.1341s | 10.9534s | 11.3702s |

Each cell averages two observations. All 24 samples remain, including slow runs. A uses control/candidate/live and its mirror; B uses candidate/live/control and its mirror, moving every mode between positions. Actual per-run orders are retained. Comparisons pair corresponding halves and are not all immediately adjacent. The two-second host observer watches competing test/profile jobs and records passive frequency/thermal context; it cannot exclude all host activity or assign a cause to slow samples. Startup includes guest work and is not an isolated compilation measure.

- long a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -5.46% throughput, paired -1.52% / -9.10%; versus baseline: -4.97% throughput, paired -0.48% / -9.12%.
- long b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +2.90% throughput, paired +1.39% / +4.33%; versus baseline: +4.61% throughput, paired +15.11% / -5.34%.
- standard a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: +1.31% throughput, paired +2.72% / -0.07%; versus baseline: +7.54% throughput, paired +0.54% / +14.46%.
- standard b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +10.78% throughput, paired +4.30% / +17.27%; versus baseline: +3.81% throughput, paired +5.19% / +2.42%.

No automatic default or delivery change follows. Separately checked diagnostics measure dispatch, dependency and guard costs; normal live/audio acceptance is required for any promotion.

## Timing decision

No promotion. Against the matching conditional-only compiler, longer-route throughput changes by -5.46% and +2.90%; standard by +1.31% and +10.78%. Longer-route comparisons with the untouched live archive also reverse (-4.97% / +4.61%). Five of eight matching half-pairs favor the candidate, but the first longer batch loses both and standard A has opposing pairs. Slow candidate and control observations remain, including the closing standard-B control that inflates its mean advantage. All 24 preplanned samples and passive host readings are retained; no watched competing benchmark job was observed. These observations do not identify the cause of timing variability. The feature remains opt-in while separately checked diagnostics measure the structural tradeoff.

## Two-mode tail-prefix census

Instrumentation passes 46,016 additional explicitly selected native fault comparisons, a checked standard image/audio replay and the checked candidate longer replay. The existing full 173-test suite also ran instrumented. Four serial diagnostics use the frozen archive at the same original limits: control enables only conditional integer leaves and candidate additionally enables bit 64. Diagnostic elapsed time is not acceptance timing.

### long

| Counter | Conditional only | Tail prefixes |
| --- | ---: | ---: |
| Compiled invocations | 140,994,669 | 136,124,130 |
| Direct-call exits | 43,526,965 | 38,656,426 |
| BX LR returns | 13,704,889 | 13,704,889 |
| Source-window ends | 3,746,623 | 3,746,623 |
| Zero-progress invocations | 1,143,911 | 1,143,911 |
| Inlined-site limit exits | 551,863 | 551,863 |
| Leaf-length limit exits | 970,429 | 970,429 |
| Primary snapshot bytes requested | 9,690,132,630 | 9,612,204,006 |
| Dependency spans requested | 35,294,035 | 40,259,602 |
| Dependency snapshot bytes requested | 768,017,640 | 847,466,712 |
| Combined primary/dependency bytes requested | 10,458,150,270 | 10,459,670,718 |
| Entry-proof attempts | 17,354,245 | 17,354,245 |
| Read spans checked | 20,261,021 | 20,261,021 |
| Write spans checked | 19,472,401 | 19,472,401 |
| Private entry-proof fallbacks | 733,238 | 733,238 |
| Fallbacks caused solely by interval gaps | 0 | 0 |

Compiled invocations change by -4,870,539 (-3.45%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 733238}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593753848, "compiled_functions": 14257, "free_bytes": 18483608, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4057, "call_inlined": 3939, "callee_mapping_extent": 5, "callee_unsupported": 3043, "inline_dependencies": 4057, "inline_site_limit": 103, "leaf_instruction_limit": 113, "source_window_filled": 291, "source_window_not_filled": 3766}`.

candidate16: entry fallback causes `{"other_guard": 733238}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593755560, "compiled_functions": 14257, "free_bytes": 18481896, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4057, "call_inlined": 3939, "callee_mapping_extent": 5, "callee_unsupported": 2985, "inline_dependencies": 4057, "inline_site_limit": 103, "leaf_instruction_limit": 113, "source_window_filled": 291, "source_window_not_filled": 3766, "tail_prefix_inlined": 58}`.

Remaining tail-prefix restrictions:

- `block_transfer`: 22,057,662.
- `pc_rn_or_rd_field`: 9,187,033.
- `multiply`: 1,476,898.
- `conditional_transfer`: 1,219,065.
- `internal_branch`: 997,800.
- `sp_rn_or_rd_field`: 642,470.
- `conditional_memory`: 631,238.
- `predicates_disabled`: 30.

Leading tail-prefix rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 5,132,775 |
| `0x70062eb0` | `push {r4, lr}` | 4,894,852 |
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
| `0x70062a78` | `ldr pc, [pc, #-4]` | 577,537 |

### standard

| Counter | Conditional only | Tail prefixes |
| --- | ---: | ---: |
| Compiled invocations | 140,626,048 | 135,499,256 |
| Direct-call exits | 44,506,411 | 39,379,619 |
| BX LR returns | 13,843,303 | 13,843,303 |
| Source-window ends | 3,915,442 | 3,915,442 |
| Zero-progress invocations | 1,138,805 | 1,138,805 |
| Inlined-site limit exits | 571,365 | 571,365 |
| Leaf-length limit exits | 1,020,658 | 1,020,658 |
| Primary snapshot bytes requested | 9,698,087,982 | 9,616,059,310 |
| Dependency spans requested | 35,765,500 | 40,996,099 |
| Dependency snapshot bytes requested | 778,289,684 | 861,979,268 |
| Combined primary/dependency bytes requested | 10,476,377,666 | 10,478,038,578 |
| Entry-proof attempts | 17,400,966 | 17,400,966 |
| Read spans checked | 19,588,182 | 19,588,182 |
| Write spans checked | 19,508,488 | 19,508,488 |
| Private entry-proof fallbacks | 741,998 | 741,998 |
| Fallbacks caused solely by interval gaps | 0 | 0 |

Compiled invocations change by -5,126,792 (-3.65%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 741998}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 594094792, "compiled_functions": 14532, "free_bytes": 18142664, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4342, "call_inlined": 4177, "callee_mapping_extent": 5, "callee_unsupported": 3260, "inline_dependencies": 4342, "inline_site_limit": 105, "leaf_instruction_limit": 124, "source_window_filled": 299, "source_window_not_filled": 4043}`.

candidate16: entry fallback causes `{"other_guard": 741998}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 594090952, "compiled_functions": 14532, "free_bytes": 18146504, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4342, "call_inlined": 4177, "callee_mapping_extent": 5, "callee_unsupported": 3189, "inline_dependencies": 4342, "inline_site_limit": 105, "leaf_instruction_limit": 124, "source_window_filled": 299, "source_window_not_filled": 4043, "tail_prefix_inlined": 71}`.

Remaining tail-prefix restrictions:

- `block_transfer`: 22,828,613.
- `pc_rn_or_rd_field`: 9,124,603.
- `multiply`: 1,582,524.
- `conditional_transfer`: 1,085,471.
- `internal_branch`: 1,040,467.
- `sp_rn_or_rd_field`: 650,421.
- `conditional_memory`: 617,447.

Leading tail-prefix rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062ad0` | `ldr pc, [pc, #-4]` | 5,388,764 |
| `0x70062eb0` | `push {r4, lr}` | 5,154,119 |
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
| `0x70062a78` | `ldr pc, [pc, #-4]` | 621,749 |

Requested byte counts describe exact comparison coverage, not physical memory traffic. Private entry-proof fallbacks differ from outer compiled returns, guest scheduling and browser yielding. Whole-emulator allocator bytes and compiled-function counts do not isolate generated-code size. Full edge/rejection/compile records, sampled guest work, footprint and diagnostic elapsed times remain in evidence; no speed conclusion is drawn from diagnostic wall time.

## Generated-module size and construction diagnostics

A separate immutable loader copy uses `instrument_modules.py --metadata-only`, with no replacements. Application WASM is byte-identical to the accepted archive; the hook observes each generated-module constructor and exports. The hooked candidate passes the 360-image native checked longer replay. The initial successful hook replay lacked a loader-hash report field; a recorder schema assertion failed and is retained. The replay harness now records that field, and a second exact hook replay verifies both loader and application WASM hashes. Two longer-route runs capture metadata from the CPU-profile-selected worker. These include startup and all construction through 60 guest seconds; they are not just the 42–60-second timing window. Instrumentation and CPU profiling exclude these elapsed times from throughput acceptance.

| Metric | Conditional only | Tail prefixes |
| --- | ---: | ---: |
| Constructed modules | 534 | 534 |
| Generated module bytes | 62,123,036 | 62,134,364 |
| Function exports, including repeat construction | 14,257 | 14,257 |
| Summed synchronous constructor milliseconds | 155.650 | 168.730 |

Generated module bytes change by +11,328 (+0.02%). Constructor duration is an observed diagnostic cost, not total browser JIT compilation time: background optimization and first-execution compilation can occur elsewhere, and a single instrumented observation per mode does not establish a repeatable compilation-speed result. All module metadata and profile-selection evidence are retained.

## Final disposition

The feature remains opt-in after the inconsistent serial timings. The checked census and module measurements explain work changes; they do not convert diagnostic wall time into a performance result.

long: 4,870,539 fewer compiled invocations (-3.45%); requested dependency bytes +10.34%, primary bytes -0.80%, combined primary/dependency coverage +0.015%; 0 gap-only entry-proof fallbacks. Invocation counts are not counts of individual guest-state loads/stores.

standard: 5,126,792 fewer compiled invocations (-3.65%); requested dependency bytes +10.75%, primary bytes -0.85%, combined primary/dependency coverage +0.016%; 0 gap-only entry-proof fallbacks. Invocation counts are not counts of individual guest-state loads/stores.

Longer-route generated module bytes change by +0.02%. The accepted conditional-only build remains served. The independent runner-cap revisit follows with every extra leaf feature disabled; no source/leaf/site/scheduling limit changes accompany it. Nothing pushed or deployed.
