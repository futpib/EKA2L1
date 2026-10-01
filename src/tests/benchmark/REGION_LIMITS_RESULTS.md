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

The primary-window control currently changes hot RAM and hot ROM translations in aot_runtime.cpp. Startup ROM exports in aot_setup.cpp retain a separate 512-byte window and, with eager regions disabled, bounded-block emission that clears forward branch targets. They can therefore return even on an in-window branch. This is a separate compiler-imposed boundary, not a guest scheduling requirement. The dominant sampled in-window edge is consistent with that path, but this census does not tag translation origin. The window sweep must not be described as changing every ROM window.

## Controls and verification

Allowed research bounds: primary bytes 128-2048 (multiple of four), leaf instructions 1-64, call sites 0-16, runner regions 0-4096. Browser controls reject invalid values and changes after initialization, then read back the applied configuration. Fault probes require an explicit matching PROBE_LIMITS marker; serial timing rejects wrong limits and enabled exit instrumentation.

The new test executes the actual runner across five caps and nine budgets, checking normal, zero-progress, stop, unmasked IRQ, masked IRQ and unavailable-successor outcomes: 270 checks. Another 640 comparisons test leaf lengths up to 65 instructions, limits up to 64, site limits up to 16, and short/interior/full budgets against the interpreter. Dependency snapshots correctly deduplicate repeated callees.

Initial harness failures are retained: the first runner test put the large ARMul_State on the WASM stack and trapped; the next test incorrectly expected one dependency per call rather than per distinct callee. Heap allocation and the corrected dependency assertion pass. These were test harness defects, not measured emulator regressions.

Acceptance is complete: all 167 compiler tests, 32 native tests, browser frontend checks, 41,280 explicitly selected native fault comparisons (13,760 per configuration), three checked 1,600-image/audio replays and the maximum-configuration 360-image longer-route replay pass. Missing or wrong limit markers are rejected. Full compiler/fault/replay status and all available logs are in REGION_LIMITS_EVIDENCE.json. Replays use the new post-merge native reference, including 4,656,051 stereo PCM frames for the 1,600-image route. The original full-route gameplay-threshold failure at the later level restart remains documented; exact replay is a different check.

## Measurement plan and costs

Serial screening varies one axis at a time: windows 256/512/1024, leaf lengths 8/16/32, sites 0/8/16, runner caps 64/512/unlimited. Same-binary defaults and the untouched merged archive are separate controls; both current scenes use forward/reverse order. No scheduling parameter changes. Every observation and exact guest-instruction/presentation count is retained. Timings have not established an optimum.

The runner control replaces a compile-time constant bound with a runtime-configurable check in the hot loop. Its contribution to the gap from the untouched archive is not isolated; both controls remain necessary.

Larger windows/leaves may increase generated code, compile work and dependency validation even if they reduce returns. Removing the runner cap retains per-region state publication, lookup and exact validation. The profiler records warmup and final compiled-function/heap counts outside the timed window; these are broad startup/resource measures, not isolated compilation time. Dedicated exit instrumentation is disabled for acceptance timing.

## Completed independent-limit screening

All forty observations are retained. Every trial within each route executes identical guest work. These are two observations per setting and route, not estimates of an optimum.

### long

Each trial: 3,031,637,220 guest instructions, 380 presentations. Seconds below are first and reverse-order samples.

| Setting | First | Reverse | Mean | Compiled functions |
| --- | ---: | ---: | ---: | ---: |
| default | 15.0739 | 15.7996 | 15.4367 | 14,345 |
| window256 | 16.5336 | 14.0533 | 15.2934 | 14,364 |
| window1024 | 12.8110 | 14.6896 | 13.7503 | 14,296 |
| leaf8 | 14.1944 | 14.8674 | 14.5309 | 14,382 |
| leaf32 | 14.7909 | 16.7873 | 15.7891 | 14,298 |
| sites0 | 16.4257 | 17.0803 | 16.7530 | 14,497 |
| sites16 | 14.3255 | 14.9606 | 14.6430 | 14,366 |
| cap64 | 12.6943 | 12.7533 | 12.7238 | 14,345 |
| uncapped | 13.3785 | 12.4046 | 12.8916 | 14,345 |
| baseline | 13.9830 | 11.8326 | 12.9078 | 14,345 |

### standard

Each trial: 2,976,635,366 guest instructions, 381 presentations. Seconds below are first and reverse-order samples.

| Setting | First | Reverse | Mean | Compiled functions |
| --- | ---: | ---: | ---: | ---: |
| baseline | 14.0793 | 17.8965 | 15.9879 | 14,621 |
| uncapped | 14.5755 | 14.3818 | 14.4787 | 14,621 |
| cap64 | 15.3139 | 14.3401 | 14.8270 | 14,621 |
| sites16 | 13.8366 | 14.6764 | 14.2565 | 14,644 |
| sites0 | 14.4793 | 18.2934 | 16.3864 | 14,818 |
| leaf32 | 13.7958 | 12.6263 | 13.2111 | 14,554 |
| leaf8 | 14.8338 | 16.3951 | 15.6144 | 14,666 |
| window1024 | 14.3560 | 16.6730 | 15.5145 | 14,545 |
| window256 | 14.6477 | 14.1730 | 14.4104 | 14,662 |
| default | 15.1980 | 12.5669 | 13.8825 | 14,621 |

These broad screens are not adjacent paired confirmations for every setting. The untouched archive and same-binary default differ materially, and unchanged variants vary between orders. No setting is promoted from this screen. The tested range is hot windows 256-1024 bytes, leaves 8-32 instructions, zero to sixteen inline sites, and runner caps 64/512/unlimited. Correctness holds at the broader tested acceptance extremes; speed optima remain unresolved. In particular, removing the runner cap does not remove per-region state publication, validation or dispatch. The census-selected conditional-leaf fusion is measured separately at the original default limits.
