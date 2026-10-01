# Single-branch callee fusion

The preserve-inner census still rejects 4,870,478 calls at one unconditional branch veneer and 901,809 at another in the longer route. This experiment admits the generic one-instruction ARM AL B shape as feature bit 32 under original-emitter policy 7 and conditional-leaf mode. It does not special-case guest addresses. Prefix bits 8/16 and broader eligibility bits 1/2/4 remain off.

The caller BL and callee B both execute and consume their exact instruction budgets. LR retains the original caller return address. Guest values remain in locals across that entry boundary, then the B uses the existing precise exit to its actual target, even when that target lies inside the primary window. The target is not resolved or assumed during translation; normal dispatch/code validation follows. Only the four veneer bytes become an additional exact dependency. Caller continuation discovery, code aliases, callback-visible state and ordered effects retain their existing paths. All limits remain 512 source bytes / 16 leaf instructions / 8 sites / 512 runner regions, with no guest scheduling change.

The expected benefit is one fewer compiled entry per eligible call. Additional dependency checks, enlarged protected intervals and generated code can outweigh that saving; separate counters and measurements are required. No performance claim follows from eligibility.

## Correctness

All 172 instrumented compiler tests pass, including the existing documented harness XFAIL. The new focused test makes 12,800 exact comparisons across both TLB indices, positive/negative/self/in-window targets, conditional caller paths, partial budgets, flags, LR, memory and caller/veneer aliases. Native tests pass 570 assertions in 33 cases, including four-byte and eight-byte dependency mutation/remapping.

Explicit feature modes 0 and 32 each match 40,640 native fault cases (81,280 total). The new 5,376-case fixture per mode checks actual four-byte fusion selection followed by faulting memory instructions and precise callbacks at the branch destination. Both checked standard replays and the normal candidate replay match all 1,600 images, guest records and 4,656,051 stereo PCM frames; the 360-image longer route also matches native and 2,832,756 PCM frames. Missing, wrong and duplicate feature/census markers are rejected.

Gameplay timing and any live/audio graduation remain pending. The served conditional-only archive is unchanged.

Instrumentation separately passes another 40,640 explicitly selected native fault comparisons and exact checked standard/longer image/audio replays before any diagnostic census. These checks do not contribute timing evidence.
