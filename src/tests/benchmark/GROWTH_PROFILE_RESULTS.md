# Diagnostic profiles of the natural length routes

Same served policy-7 archive and guest seconds 42–60 as GROWTH_LENGTH_RESULTS.md.
CPU sampling and detailed guest counters ran separately, serially. Their elapsed
times are diagnostics and do not replace the ordinary throughput observations.

| Guest-worker CPU samples | Short | Longer |
| --- | ---: | ---: |
| Generated code and callees, inclusive | 42.69% | 42.96% |
| InterpreterMainLoop self (includes inlined compiled runner) | 19.69% | 19.52% |
| Validated cache find self | 13.11% | 13.68% |
| Exact byte comparison self | 10.55% | 10.88% |

Inclusive and self categories must not be added as independent recoverable costs.
These are one sampled run per route, not a causal partition of the time difference.
Their broad distributions are similar; the longer scene does more work across
several paths rather than exposing one uniquely dominant new function.

Separate guest counters report 169,718,533 compiled calls for short versus
199,922,265 for longer. Only 35,179,732 and 35,800,043 guest instructions execute
interpreted. Yet 10,891,710 and 11,043,032 compiled invocations return zero count.
Sampled zero-count events overwhelmingly identify 0x700002b8, whose instruction
is LDR r6,[PC,#0x14]. The following five-instruction sequence dominates interpreter
samples. This is an observed fallback site, not an explanation of why its guard
fails. Detailed guest profiling also adds mapping lookups and substantial overhead.

The literal-load fallback is a concrete next diagnostic target. Determine whether
it fails a memory guard, budget proof or another condition before selecting a
change. Any optimization must remain general and preserve permission checks,
ordered callbacks, exact code validation and precise instruction counts.

Raw CPU and guest profiles: growth-short-cpu-profile, growth-long-cpu-profile,
growth-short-guest-profile and growth-long-guest-profile under the scratch benchmark
root. GROWTH_PROFILE_EVIDENCE.json records reports, samples and hashes.
