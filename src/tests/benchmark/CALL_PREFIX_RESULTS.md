# Callee-prefix fusion

Implementation is opt-in and passes correctness acceptance. Performance and live/audio acceptance remain pending. The preceding six-run census is committed as 984133126. It found roughly 10.0/10.5 million rejected calls to three captured stack-frame routines whose first nested calls occur within seven instructions. These counts identify possible coverage; they do not predict a speedup.

Feature bit 8 admits a bounded straight callee prefix ending at an unconditional direct BL. Ordinary integer operations, scalar word/byte memory operations and non-PC block transfers use the existing original emitter. Stack saves, SP changes and saved LR values remain real guest state and memory effects. Conditional operations preserve their ordinary predicates and instruction counts. Other control transfers, loaded returns, status/coprocessor instructions and unsupported encodings before the nested call remain rejected.

The initial caller BL and prefix execute in the same generated function with guest values retained in locals. The nested BL still takes the existing precise exit and publishes its real guest PC, LR and count. The original caller continuation is not treated as fallthrough; both that return entry and the nested call's return entry are recorded. A prefix does not claim to fuse the whole nested call chain, eliminate all state publication, or change guest scheduling. Primary/dependency validation, code-alias guards and callback ordering remain required.

The independent feature is tested at the original 16-instruction leaf bound, with conditional integer leaves enabled and broader feature bits 1/2/4 disabled. A matching feature-0 control and the untouched merged archive remain comparison controls. No default is changed.

The focused suite currently passes 36,960 interpreter comparisons across all integer conditions, partial budgets, alternate caller paths, stack/data mapping misses, read-only stores and physical aliases of caller/callee code. It verifies the precise nested-call exit and absence of premature caller continuation. The new native fault fixture covers saved LR/SP and conditional flags visible at scalar fault callbacks, then continues at the nested target through the real runner. Full compiler/fault/image-audio acceptance passes as detailed below.

Diagnostics label prefix exits separately as `inlined_prefix_call`. The previously unlabelled forward target beyond the first BX LR now has the explicit structural label `forward_target_after_return`; its refusal behavior is unchanged.

## Correctness acceptance

All 171 instrumented compiler tests pass (the existing documented harness XFAIL remains), with 36,960 focused prefix comparisons also passing without instrumentation. All 33 native tests pass (548 assertions). The two explicitly selected feature modes match native for 29,888 fault cases each, 59,776 total, including saved-stack/LR and nested-call callback cases. Both checked standard replays and the normal candidate replay match 1,600 images, guest records and 4,656,051 stereo PCM frames. The checked candidate longer route matches 360 images and audio. Missing, wrong and duplicate feature/census markers are rejected. All archive hashes, source patch and accepted logs are retained in CALL_PREFIX_EVIDENCE.json. No timing or delivery claim follows from these checks.
