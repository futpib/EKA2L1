# Compile deferred memory misses

An opt-in shared compiler policy sends failed scalar and non-writeback multi-register memory guards through the existing memory helpers instead of restarting the guest instruction in the interpreter. Callback publication/reload, code-write tracking, mapping/lifetime checks and instruction/region limits remain in place. No game or guest address selects the policy.

Lazy multiply pairs are materialized before a load that can now call a helper. This preserves callback visibility and callback-written values, but may cost extra hot-path work; coverage is not a speed claim. The option is frozen before initialization, default off, with explicit harness readback and rejection of post-initialization changes: `EKA2L1_COMPILED_MEMORY_MISSES=1`.

Acceptance:

- All 187 compiler tests, including 6,148 new full-state/memory/budget/callback comparisons.
- 5,376 selected native memory-fault comparisons in modes 0/3 and both TLB layouts, including lazy multiply prefixes. Every tested guest instruction stays compiled; callback events, registers, flags, counts and exact memory agree.
- 20,736 syscall and 17,280 exclusive comparisons, 4,032 existing ARM fault cases, 2,880 Thumb memory faults and 1,152 Thumb call cases. Existing probes retain their explicitly selected policies; the new memory probes exercise this option.
- Three native package targets and seven exact native image/frame-record/PCM replays: both Sky routes normal/checked, both Snakes routes and matching stationary control.

The first new probe matched all 672 cases, but its inherited final assertion required at least one deferred instruction. The selected policy removes that fallback, so the assertion is now option-sensitive while per-case all-compiled checks remain mandatory. That failed run is retained. The first full suite also exposed that the new test implicitly selected the default IR-segment policy rather than production policy7. Those separate guarded IR exits remain outside this scalar/span option. V23c selects policy7 explicitly and reruns the entire suite. Production binaries remain byte-identical to V23; production replays and counters are reused. Native memory probes were rerun after the V23b probe correction and their artifacts are unchanged in V23c.

Diagnostic census (not wall-time evidence):

| Route | Guest instructions | Interpreted | Share |
|---|---:|---:|---:|
| sky | 2,142,147,961 | 985,679 | 0.046014% |
| combat | 2,171,043,925 | 414,030 | 0.019071% |
| standard | 2,964,235,296 | 4,824,927 | 0.162771% |
| long | 3,031,637,220 | 902,875 | 0.029782% |

All counts match independent total-minus-compiled counters. Changed dispatch patterns also affect cache readiness: removing one fallback need not monotonically lower total interpretation. Zero interpretation and realtime are not claimed. A serial four-route timing screen follows before any promotion; live remains unchanged and nothing is pushed.
