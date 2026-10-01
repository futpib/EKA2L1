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
