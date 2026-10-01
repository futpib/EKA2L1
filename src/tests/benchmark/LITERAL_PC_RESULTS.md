# Runtime literal-PC veneer fusion

The tail-prefix census still finds frequent calls to an unconditional `LDR PC,[PC,#imm]` veneer: the leading instance accounts for 5.13/5.39 million longer/standard rejected calls. This is a motivation and upper coverage clue, not a speed estimate. The implementation recognizes the instruction encoding generically; no guest address is special-cased.

Feature 128 admits one unconditional immediate, pre-indexed word load to PC from PC without writeback. It preserves the runtime literal read, ordinary TLB/helper path, callback-visible state, interworking bit, instruction count and precise indirect exit. The loaded destination is not constant-folded. Only the four instruction bytes become a code dependency; neither literal contents nor target code are assumed. The caller's return address is retained. Missing mappings and failed accesses still use existing precise callbacks. The existing emitter performs the load, so this changes eligibility rather than introducing a new memory lowering.

Policy 7 and conditional integer leaves stay enabled. Source/leaf/site/runner limits stay 512/16/8/512; all other extra eligibility features are disabled. The new bit defaults off. Guest scheduling is unchanged.

## Focused verification

- All 174 instrumented compiler tests pass, including 15,360 dedicated interpreter comparisons. The documented pre-existing crash-repro harness XFAIL remains in the log.
- The dedicated test also passes alone. It reuses translated bytes across changed literal destinations and mapped backing pages, checks short budgets, both cache indices, positive/negative literal offsets, ARM/Thumb targets, flags, registers, memory and callback-visible PC/LR. It rejects conditional, byte, post-indexed, writeback, register-indexed and non-PC-base forms. The standalone and full-suite counts overlap.
- Control, candidate and instrumented candidate each match 5,376 independent native fault cases (16,128 comparisons, with repeated coverage across modes). Full callback events, state, memory, instruction count and explicit feature/limit selection match. Both endian modes and repaired, failed, stopped and unresolved-retry callbacks are included. The optional fixture leaves existing archived default matrices unchanged.
- Native/cache tests pass 570 assertions in 33 cases. Launcher tests verify feature 128 configuration/readback, rejected markers and feature-dependent ETags; the parser permits exactly 0–255.
- Native and WASM builds pass. Existing linker warnings remain in the archived logs; they are not represented as clean builds without warnings.

The immutable archive and source patch, binary hashes, scripts, raw fault output hashes and test logs are retained in the evidence. No full browser replay or performance result is claimed at this checkpoint.

## Planned acceptance and timing

After the runner-cap study completes, run full control/candidate native fault matrices (46,016 each, including the optional fixture), checked control/candidate and normal candidate standard image/audio replays, and the checked longer route. Missing or wrong policy readback must fail. Then collect exactly 24 serial observations over both routes with forward and rotated/reversed ordering, comparing feature 128 with feature 0 in the same binary and the untouched served archive. All samples and passive host readings remain. Builds and diagnostics stay outside timing.

A separately checked census will measure compiled calls, indirect exits, dependency spans/bytes, primary byte coverage, entry proofs and code/interval-gap guards. Generated-module size and construction costs will be measured separately. Any apparent throughput gain must survive normal/live/audio and actual HTTPS/cache-upgrade acceptance before delivery. Nothing is pushed or deployed by this experiment.

The preserved completion queue now includes metadata-only generated-module captures after the exit census, with an exact native checked replay of the hooked loader first. Disjoint control/candidate correctness matrices can run concurrently; all must finish before replays and serial timing. No timing order or frozen binary changes. Final disposition records every sample and requires manual live/audio review if comparisons consistently favor the candidate; it never deploys automatically. These are queued procedures, not completed results.

## Full correctness acceptance

Both control and candidate pass 46,016 explicitly selected native fault comparisons (92,032 total), including the runtime literal-load fixture. Checked control/candidate and normal candidate standard replays match all 1,600 native images, guest records and 4,656,051 PCM frames. The checked longer route matches 360 images and 2,832,756 PCM frames. Actual compiler policy, feature mask, limits and archive hashes are verified. These full matrices include cases also reported by the focused runs; no extra distinct coverage is implied.

No new timing or live/audio acceptance is claimed yet. The previous conditional-only archive stays served.

## Serial gameplay timing

All modes retain policy 7, conditional integer leaves and limits 512/16/8/512. The matching control uses feature 0 and the candidate feature 128 in the same frozen binary. The untouched delivered conditional-only archive is a separate baseline. Diagnostics and interpreter checking are off; guest work and scheduling are identical within each route.

| Route/batch | Matching control | Literal PC veneers | Untouched live archive |
| --- | ---: | ---: | ---: |
| long a | 11.8977s | 11.9295s | 11.2860s |
| long b | 11.6942s | 11.1486s | 11.3666s |
| standard a | 11.1696s | 11.3590s | 11.3153s |
| standard b | 11.6703s | 11.0264s | 11.2378s |

Each cell averages two observations. All 24 samples remain, including slow runs. A uses control/candidate/live and its mirror; B uses candidate/live/control and its mirror, moving every mode between positions. Actual per-run orders are retained. Comparisons pair corresponding halves and are not all immediately adjacent. The two-second host observer watches competing test/profile jobs and records passive frequency/thermal context; it cannot exclude all host activity or assign a cause to slow samples. Startup includes guest work and is not an isolated compilation measure.

- long a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -0.27% throughput, paired -1.05% / +0.43%; versus baseline: -5.39% throughput, paired +1.55% / -11.61%.
- long b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +4.89% throughput, paired +0.65% / +9.25%; versus baseline: +1.95% throughput, paired -1.25% / +5.25%.
- standard a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -1.67% throughput, paired +1.52% / -4.65%; versus baseline: -0.38% throughput, paired +1.69% / -2.33%.
- standard b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +5.84% throughput, paired +1.10% / +10.57%; versus baseline: +1.92% throughput, paired +1.91% / +1.92%.

No automatic default or delivery change follows. Separately checked diagnostics measure dispatch, dependency and guard costs; normal live/audio acceptance is required for any promotion.

## Two-mode literal-pc census

Instrumentation passes 46,016 additional explicitly selected native fault comparisons, a checked standard image/audio replay and the checked candidate longer replay. The existing full 174-test suite also ran instrumented. Four serial diagnostics use the frozen archive at the same original limits: control enables only conditional integer leaves and candidate additionally enables bit 128. Diagnostic elapsed time is not acceptance timing.

### long

| Counter | Conditional only | Literal PC veneers |
| --- | ---: | ---: |
| Compiled invocations | 140,994,669 | 131,883,365 |
| Direct-call exits | 43,526,965 | 34,417,291 |
| BX LR returns | 13,704,889 | 13,704,848 |
| Source-window ends | 3,746,623 | 3,745,880 |
| Zero-progress invocations | 1,143,911 | 1,143,425 |
| Inlined-site limit exits | 551,863 | 584,858 |
| Leaf-length limit exits | 970,429 | 970,368 |
| Primary snapshot bytes requested | 9,690,132,630 | 9,652,675,872 |
| Dependency spans requested | 35,294,035 | 49,011,838 |
| Dependency snapshot bytes requested | 768,017,640 | 822,884,976 |
| Combined primary/dependency bytes requested | 10,458,150,270 | 10,475,560,848 |
| Entry-proof attempts | 17,354,245 | 17,352,455 |
| Read spans checked | 20,261,021 | 20,260,461 |
| Write spans checked | 19,472,401 | 19,471,287 |
| Private entry-proof fallbacks | 733,238 | 733,211 |
| Fallbacks caused solely by interval gaps | 0 | 0 |

Compiled invocations change by -9,111,304 (-6.46%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 733238}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593754624, "compiled_functions": 14257, "free_bytes": 18482832, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4057, "call_inlined": 3939, "callee_mapping_extent": 5, "callee_unsupported": 3043, "inline_dependencies": 4057, "inline_site_limit": 103, "leaf_instruction_limit": 113, "source_window_filled": 291, "source_window_not_filled": 3766}`.

candidate16: entry fallback causes `{"other_guard": 733211}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 593697864, "compiled_functions": 14235, "free_bytes": 18539592, "guest_us": 60000000, "instructions": 9476989025}`; lifetime compile events `{"accepted_regions": 4032, "call_inlined": 3820, "callee_mapping_extent": 5, "callee_unsupported": 2466, "inline_dependencies": 4032, "inline_site_limit": 104, "leaf_instruction_limit": 128, "literal_pc_veneer_inlined": 575, "source_window_filled": 290, "source_window_not_filled": 3742}`.

Remaining literal-pc restrictions:

- `block_transfer`: 22,052,452.
- `internal_branch`: 5,868,447.
- `multiply`: 1,476,840.
- `conditional_transfer`: 1,219,133.
- `sp_rn_or_rd_field`: 642,607.
- `conditional_memory`: 631,314.
- `pc_rn_or_rd_field`: 49,431.
- `predicates_disabled`: 9.

Leading literal-pc rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062eb0` | `push {r4, lr}` | 4,894,852 |
| `0x70003c88` | `b #0x7006370c` | 4,870,621 |
| `0x70064c9c` | `push {r4, lr}` | 2,642,205 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,483,882 |
| `0x700653e0` | `push {r4, lr}` | 2,225,670 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,225,596 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,331,138 |
| `0x700641f8` | `blt #0x70064208` | 1,217,466 |
| `0x70003c78` | `b #0x70063394` | 901,809 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 882,552 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 821,621 |
| `0x70065c04` | `mul r1, r2, r1` | 654,838 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 654,611 |
| `0x70004f18` | `str lr, [sp, #-4]!` | 513,796 |
| `0x70064848` | `ldreq r0, [r0, #4]` | 398,272 |
| `0x7002c6b8` | `push {r4, r5, r6, r7, lr}` | 397,918 |

### standard

| Counter | Conditional only | Literal PC veneers |
| --- | ---: | ---: |
| Compiled invocations | 140,626,048 | 131,572,108 |
| Direct-call exits | 44,506,411 | 35,453,863 |
| BX LR returns | 13,843,303 | 13,842,115 |
| Source-window ends | 3,915,442 | 3,915,316 |
| Zero-progress invocations | 1,138,805 | 1,139,025 |
| Inlined-site limit exits | 571,365 | 595,325 |
| Leaf-length limit exits | 1,020,658 | 1,020,877 |
| Primary snapshot bytes requested | 9,698,087,982 | 9,661,840,090 |
| Dependency spans requested | 35,765,500 | 48,761,335 |
| Dependency snapshot bytes requested | 778,289,684 | 830,279,112 |
| Combined primary/dependency bytes requested | 10,476,377,666 | 10,492,119,202 |
| Entry-proof attempts | 17,400,966 | 17,400,313 |
| Read spans checked | 19,588,182 | 19,587,756 |
| Write spans checked | 19,508,488 | 19,507,306 |
| Private entry-proof fallbacks | 741,998 | 742,076 |
| Fallbacks caused solely by interval gaps | 0 | 0 |

Compiled invocations change by -9,053,940 (-6.44%). This counts region entries/returns, not individual state stores or loads.

control16: entry fallback causes `{"other_guard": 741998}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 594095696, "compiled_functions": 14532, "free_bytes": 18141760, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4342, "call_inlined": 4177, "callee_mapping_extent": 5, "callee_unsupported": 3260, "inline_dependencies": 4342, "inline_site_limit": 105, "leaf_instruction_limit": 124, "source_window_filled": 299, "source_window_not_filled": 4043}`.

candidate16: entry fallback causes `{"other_guard": 742076}`; overlap spans `{}`; post-store guard outcomes `{}`.

End-of-run footprint `{"allocated_bytes": 594084968, "compiled_functions": 14503, "free_bytes": 18152488, "guest_us": 78000000, "instructions": 11957513019}`; lifetime compile events `{"accepted_regions": 4316, "call_inlined": 4153, "callee_mapping_extent": 5, "callee_unsupported": 2726, "inline_dependencies": 4316, "inline_site_limit": 115, "leaf_instruction_limit": 121, "literal_pc_veneer_inlined": 586, "source_window_filled": 291, "source_window_not_filled": 4025}`.

Remaining literal-pc restrictions:

- `block_transfer`: 22,824,598.
- `internal_branch`: 6,167,305.
- `multiply`: 1,582,499.
- `conditional_transfer`: 1,085,507.
- `sp_rn_or_rd_field`: 650,579.
- `conditional_memory`: 617,582.
- `pc_rn_or_rd_field`: 51,450.
- `predicates_disabled`: 19.

Leading literal-pc rejected opcodes:

| Guest PC | Instruction | Calls |
| --- | --- | ---: |
| `0x70062eb0` | `push {r4, lr}` | 5,154,119 |
| `0x70003c88` | `b #0x7006370c` | 5,126,798 |
| `0x70064c9c` | `push {r4, lr}` | 2,765,034 |
| `0x70004f8c` | `push {r4, r5, r6, lr}` | 2,588,975 |
| `0x700653e0` | `push {r4, lr}` | 2,358,923 |
| `0x70003d44` | `push {r4, r5, r6, lr}` | 2,358,844 |
| `0x70004eac` | `push {r2, r3, r4, r5, r6, lr}` | 1,222,388 |
| `0x700641f8` | `blt #0x70064208` | 1,084,032 |
| `0x70003c78` | `b #0x70063394` | 941,034 |
| `0x70065be0` | `smull r1, r2, r3, r1` | 900,725 |
| `0x7000b574` | `push {r4, r5, r6, r7, r8, sb, sl, lr}` | 799,081 |
| `0x70065c04` | `mul r1, r2, r1` | 681,393 |
| `0x7002c8d4` | `push {r4, r5, r6, lr}` | 680,782 |
| `0x70004f18` | `str lr, [sp, #-4]!` | 544,300 |
| `0x7002c6b8` | `push {r4, r5, r6, r7, lr}` | 408,859 |
| `0x7002f0a0` | `push {r0, r1, r2, r3, r4, r5, r6, lr}` | 397,787 |

The user-directed mode-3 LAN rollout was completed separately as 0a030f816 while this census was held. All four resumed runs explicitly select and report mode 0; the original frozen archive and guest work remain unchanged. These comparisons do not measure the new delivered mode-3 default.

Requested byte counts describe exact comparison coverage, not physical memory traffic. Private entry-proof fallbacks differ from outer compiled returns, guest scheduling and browser yielding. Whole-emulator allocator bytes and compiled-function counts do not isolate generated-code size. Full edge/rejection/compile records, sampled guest work, footprint and diagnostic elapsed times remain in evidence; no speed conclusion is drawn from diagnostic wall time.

The first report-recording attempt used system Python without Capstone and failed before reading measurements. Its traceback is retained. Recording then used the existing census analysis environment; all measurements remain unchanged.

## Generated-module size and construction diagnostics

A separate immutable loader copy uses `instrument_modules.py --metadata-only`, without replacing generated modules. Application WASM is byte-identical to the accepted archive. The hooked candidate passes the exact 360-image native checked longer replay including guest records and PCM; both application and loader hashes are verified.

Two longer-route runs capture metadata from the CPU-profile-selected worker. Counts include startup and construction through 60 guest seconds, beyond the 42–60-second timing window. Hook and profiling elapsed times are not performance acceptance samples.

| Metric | Conditional only | Literal-PC veneer fusion |
| --- | ---: | ---: |
| Constructed modules | 534 | 536 |
| Generated module bytes | 62,123,036 | 61,391,262 |
| Function exports, including repeat construction | 14,257 | 14,235 |
| Summed synchronous constructor milliseconds | 147.600 | 147.425 |

Generated module bytes change by -731,774 (-1.18%). Constructor time is an observed diagnostic cost, not total browser JIT compilation: background optimization and first-execution compilation can occur elsewhere. One instrumented sample per mode does not establish a repeatable compilation-speed result. Raw metadata and worker-selection evidence are retained.

## Disposition

The preplanned comparisons do not consistently favor the candidate against matching and untouched controls on both routes and orders. It does not earn promotion. All 24 observations, including slower candidate/control runs, remain. The feature stays disabled; no new live/audio or deployment acceptance is claimed for this rejected policy.

long-a: -0.27% throughput versus control; -5.39% throughput versus baseline.

long-b: +4.89% throughput versus control; +1.95% throughput versus baseline.

standard-a: -1.67% throughput versus control; -0.38% throughput versus baseline.

standard-b: +5.84% throughput versus control; +1.92% throughput versus baseline.

Boundary savings and requested comparison bytes are mechanism evidence, not a substitute for gameplay timing. Guest scheduling and all four execution limits remain unchanged. No push or deployment.
