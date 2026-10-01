# Callee-prefix fusion

Implementation is opt-in and passes correctness acceptance. Serial gameplay timing does not support promotion; the feature remains opt-in. The follow-up diagnostic census is pending. The preceding six-run census is committed as 984133126. It found roughly 10.0/10.5 million rejected calls to three captured stack-frame routines whose first nested calls occur within seven instructions. These counts identify possible coverage; they do not predict a speedup.

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

The paired diagnostic census follows these timings to measure removed boundaries and added dependency/proof work. No default change, live/audio graduation or deployment follows timing alone.

All four batch means favor the matching conditional-only control, and seven of eight adjacent matching pairs favor that control. Prefix fusion therefore does not earn promotion. The reversed standard batch contains one faster candidate observation; it is retained alongside all slower observations. No live/audio graduation is pursued for this prefix extension on this evidence. The preceding conditional-only candidate remains a separate graduation candidate.
