# Current original emitter and mixed IR: diagnostic profiles

Policies 7 and 13 were profiled serially within the identical ir-conditions-candidate
application, over guest seconds78-96 with shared audio, hardware GPU, detailed
counters off and5ms CDP CPU sampling. Both execute3,975,618,624 guest instructions
and676 presentations. No owned build/test/emulator overlapped either run.
These are diagnostic samples, not promotion timings or an isolated causal estimate.

| Guest worker samples | Original7 | Conditional IR13 |
| --- | ---: | ---: |
| Entire sampled span | 13.415s | 17.202s |
| Generated code including callees | 6.230s (46.44%) | 8.403s (48.85%) |
| Outer CPU loop self | 2.220s (16.55%) | 2.777s (16.14%) |
| Validated cache lookup self | 1.734s (12.93%) | 2.297s (13.35%) |
| Exact byte comparison self | 1.401s (10.44%) | 1.806s (10.50%) |

The slower IR diagnostic also has slower surrounding lookup/validation samples;
these two observations cannot attribute the whole difference to generated IR.
The broad cost composition remains similar. Generated-inclusive values overlap
callees and must not be summed with arbitrary self categories. InterpreterMainLoop
contains compiled dispatch; its samples are not proof of interpreter fallback.
Profiles attach33 isolates; use the identified busy worker separately, not a sum
of overlapping workers. Sample spans include profiler boundaries and waits.

The two captured busy regions remain visible, but together occupy only about6%
of each sampled worker. Improving those two alone cannot remove the substantial
remaining dispatch/validation and other generated-code costs. This supports
continued work on both execution and region boundaries, without promising a gain.

Static inspection finds multiple32-instruction budget guards in the IR fixtures;
the source explicitly caps segment formation at32. Increasing that bounded cap
is a next hypothesis, with compilation-size/register-pressure tradeoffs. Native
fixture7 metadata and all profiles/hashes are preserved in the evidence JSON.
No application change or deployment is made by this diagnostic.
