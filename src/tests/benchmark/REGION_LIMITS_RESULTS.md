# Independent region and runner limits

The default remains a 512-byte primary window, 16-instruction leaves, eight inlined call sites and 512 regions per runner invocation. The new pre-init control changes these independently. Runner zero removes only its region-count cap; guest budgets, interrupts, stop requests, zero-progress rejection and successor validation remain. Guest scheduling is untouched. No deployment or speed claim.

## Refined diagnostic census

| Direct-call exit reason | Long 42-60s | Standard 60-78s |
| --- | ---: | ---: |
| callee_unsupported | 63,938,497 | 65,636,659 |
| conditional_call | 921,540 | 857,751 |
| inline_site_limit | 105,426 | 114,191 |
| leaf_instruction_limit | 970,487 | 1,020,702 |
| no_leaf_resolver | 380 | 381 |

These reasons are emitted at the actual call site and counted on every call exit; they replace the earlier ambiguous correlation with lifetime translation events. Both routes reproduce the prior exact exit totals. No sampled edges were dropped. Refusal totals reconcile with direct-call exits.

Two frequent six-instruction callees load two values, compare them, execute a complementary conditional-MOV pair, then BX LR. Conditional instructions make the present leaf validator reject them. Together they account for 18,474 of 64,556 sampled direct-call exits (28.6%) in the long scene. Captured bytes and addresses are diagnostic evidence, not production special cases. A generic conditional-integer leaf extension is the next concrete fusion candidate; it is not part of this limit-control change.

Source-window-end exits are not all window-imposed boundaries. Some external branches also leave the window. Of 25,300 sampled long-scene external direct branches, 752 target 512-1024 bytes ahead of entry, and 1,062 target 1024-2048 bytes ahead. Another 3,712 target inside the nominal 512-byte window, reflecting other CFG/mapping restrictions; 11,419 target before entry and 8,355 beyond 2048 bytes. These distances are screening evidence, not proof that enlarging a window can fuse the edge. The raw standard-route breakdown is retained too.

## Controls and verification

Allowed research bounds: primary bytes 128-2048 (multiple of four), leaf instructions 1-64, call sites 0-16, runner regions 0-4096. Browser controls reject invalid values and changes after initialization, then read back the applied configuration. Fault probes require an explicit matching PROBE_LIMITS marker; serial timing rejects wrong limits and enabled exit instrumentation.

The new test executes the actual runner across five caps and nine budgets, checking normal, zero-progress, stop, unmasked IRQ, masked IRQ and unavailable-successor outcomes: 270 checks. Another 640 comparisons test leaf lengths up to 65 instructions, limits up to 64, site limits up to 16, and short/interior/full budgets against the interpreter. Dependency snapshots correctly deduplicate repeated callees.

Initial harness failures are retained: the first runner test put the large ARMul_State on the WASM stack and trapped; the next test incorrectly expected one dependency per call rather than per distinct callee. Heap allocation and the corrected dependency assertion pass. These were test harness defects, not measured emulator regressions.

At this checkpoint, the focused runner/inlining checks, native tests and browser frontend pass; the default and maximum configurations each pass 13,760 explicit-policy fault comparisons and an exact checked 1,600-image/audio replay. The full compiler suite, minimum configuration and maximum long-route replay remain in progress. Full compiler/fault/replay status and all available logs are in REGION_LIMITS_EVIDENCE.json. Replays use the new post-merge native reference, including 4,656,051 stereo PCM frames for the 1,600-image route. The original full-route gameplay-threshold failure at the later level restart remains documented; exact replay is a different check.

## Measurement plan and costs

Serial screening varies one axis at a time: windows 256/512/1024, leaf lengths 8/16/32, sites 0/8/16, runner caps 64/512/unlimited. Same-binary defaults and the untouched merged archive are separate controls; both current scenes use forward/reverse order. No scheduling parameter changes. Every observation and exact guest-instruction/presentation count is retained. Timings have not established an optimum.

Larger windows/leaves may increase generated code, compile work and dependency validation even if they reduce returns. Removing the runner cap retains per-region state publication, lookup and exact validation. The profiler records warmup and final compiled-function/heap counts outside the timed window; these are broad startup/resource measures, not isolated compilation time. Dedicated exit instrumentation is disabled for acceptance timing.
