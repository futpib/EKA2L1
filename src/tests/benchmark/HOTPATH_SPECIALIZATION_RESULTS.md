# Frozen hot-path specialization: V29 acceptance

All options remain disabled by default. This stage changes shared runtime selection, with coverage-only options and ROM dispatch disabled in the normal comparison. Guest scheduling, instruction/region limits and executable-byte policy are unchanged.

| Item | Implementation and emitted-WASM evidence | Disposition |
|---|---|---|
| Verification | Select a nonverified lookup specialization at the outer runner. Normal emitted lookup lacks the validation_running load; verified/reference paths retain it. | Correctness accepted; timing pending |
| Cache policy | Select trusted bytes plus original cache layout outside repeated lookups. Emitted specialized cache lacks the executable-byte-policy load and byte scans; generation/source, ASID, live bit, resolver, backing and extent checks remain. Other layouts and compatibility modes retain the general path. | Correctness accepted; timing pending |
| Outer diagnostics | Select a quiet outer loop only when PC sampling, crash history, detailed counters and guest profiling are disabled. Five frozen diagnostic flag loads occur at selection, absent from its repeated body. Instrumented body retains dynamic phase checks. | Correctness accepted; timing pending |
| Guard publication | Reuse existing mode-aware omission option. Emitted omission lookup has zero interval stores at offsets 856/860; publication paths retain three static stores (five profiled). Modes 0/1 continue publishing. | Fresh gates accepted; interrupted prior timing is not a gain result |

No new hot function-pointer dispatch was added. Only the useful original-layout trusted-byte cache case is specialized. Binaryen inlines the quiet and instrumented outer templates into one public function. `sync_write_protection()` was already compiled away and is not counted as a removal.

Fresh acceptance:

- 192 compiler tests with mask 7; 16,384 cache lifecycle oracle comparisons and 288 real lookup/runner guard-publication comparisons across modes and selectors.
- 16 active/inactive reference-interpreter protection checks, run in a separate process with C environment verification enabled.
- Native probes: 5,632 interworking, 5,376 cold-memory, 20,736 syscall, 17,280 exclusive, 1,152 Thumb call, 2,880 Thumb memory and 4,032 ARM memory cases; three native CTest targets. Existing Thumb oracle files are explicitly reused; browser/compiler tests exercise new selection paths.
- 27 exact native image, instruction-record and PCM replays: all four routes for control, each individual switch and all-switch combination; checked Sky Force routes and mode-0 Snakes compatibility. Actual option readback, invalid-input rejection and post-initialization rejection pass.
- Separate detailed counter runs at masks 0/7 preserve identical guest, interpreted, outer compiled-entry and constituent-block counts across warmup/measurement phases. Their overlapping diagnostic wall times are not speed evidence.

The first reference-protection test launch did not inject its host environment into Emscripten C ENV. Its failure is retained. A preRun injection reran the byte-identical test artifact successfully; no emulator or expected result changed.

Static production WASM grows from 11,059,955 to 11,136,984 bytes: +77,029 bytes (+0.696%). This is the cost of the combined experimental binary containing all selectable implementations; it does not isolate a per-item binary size. Same-binary switch timings will isolate runtime effects. Startup/warmup and allocator footprint are retained per observation. A final trimmed combination needs a fresh total-change comparison before deployment.

Recovered guard-publication acceptance: cc4092f84; stopped panel: 69d6416be, eight completed observations. Those results remain historical, not freshly rerun or promoted.

Next: four independent reversed-order panels, each covering stationary and moving/firing Sky Force and standard/longer Snakes. Hold the faster shared policy fixed. Preserve every sample, then test a useful combination. The user explicitly permits a modest loss in either game for a large repeatable gain in the other; zero regression is not a gate. Keeping Snakes realtime and reaching Sky Force realtime remain targets. Current V28c normal-control unpaced speeds are Sky Force 0.555–0.566x stationary / 0.574–0.590x combat and Snakes 1.689–1.716x standard / 1.742–1.803x longer. V29 is untimed; realtime remains unachieved. Nothing deployed or pushed.
