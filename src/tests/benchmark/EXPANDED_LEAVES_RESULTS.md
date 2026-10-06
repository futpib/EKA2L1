# Expanded original-emitter leaf prototype

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The paired census is retained as a9709c0d2 after all conditional-leaf timing batches (a8c0f5531). This opt-in prototype passes correctness acceptance below, but its broader eligibility does not show a repeatable marginal gameplay gain. It remains disabled by default; live/audio graduation has not been performed.

Use separate feature bits under original-emitter policy 7: multiply forms; scalar extra/conditional memory forms; forward internal branches. Preserve the existing conditional-integer switch as the matching control. No runtime scheduling changes and no default enablement.

The highest remaining broad buckets (stack-frame routines and veneers) require nested-call/return-target handling that existing straight-line flattening cannot safely provide. Smaller concrete opportunities are already supported by the emitter: about 1.48 million multiply rejections; about 0.63 million conditional memory rejections; about 1.22 million conditional transfers, dominated by a forward-branching lookup. Branch support is generic, not keyed to these addresses.

For an initial acyclic shape, validate a bounded callee ending in unconditional BX LR, no LR modification, no external/backward/nested-call transfer. All accepted B targets must be forward and inside the final validated extent. Create call-site-specific local block labels, close them at actual callee guest PCs, and branch inside those labels without publishing/reloading the guest register cache. Keep per-instruction COUNT and exact PC/LR snapshots; current budget chunk builder already excludes leaf instructions. Force exit checks at all joins, independent of lexical predecessor. Preserve ordered memory helpers and dependency validation. Scalar halfword loads/stores and conditional memory reuse existing lowering but retain all emitter restrictions (exclude doubleword, exclusive/status/unpredictable forms). MUL/MLA/long multiply decode real operands rather than treating raw fields as generic ALU operands.

A separately interpreter-checked 32-instruction probe captured the full lookup: 19 instructions, two forward joins, a halfword load, and final BX LR. The focused fixture uses those captured bytes at a relocated address and two call sites. That checked probe is byte-capture evidence, not a matching counter/timing comparison with the four paired diagnostic censuses, which had verification disabled. Test eligibility at the default bound and independently at 32; Compare the 32-instruction control with the 32-instruction extended compiler, and compare the extended compiler at 16 and 32 instructions. Recount site/length limits after expansion. Invalid branches, joins following stores, false conditions, short budgets, callback remaps and code/dependency writes need explicit interpreter/fault comparisons before timing.

The initial focused tests pass 29,120 synthetic comparisons; adding the captured two-call lookup passes 29,795. The final focused matrix adds signed/unsigned long multiplication, accumulation/flags and conditional stores and passes 88,035 exact comparisons. Native/WASM builds and all 33 native tests pass; the completed broader gates are recorded below. No speed claim is made. All features remain disabled by default.

An additional unconditional-forward-branch run passes 6,915 comparisons (including the captured lookup again). Its auxiliary test artifact is separate from the immutable application archive; production code is unchanged.

The first new fault-fixture run stopped with features disabled because it inherited the older fixture's minimum of four compiled instructions. Its memory operation occurs one instruction earlier: two caller instructions plus the callee branch precede a fault. The corrected fixture accepts a minimum of three, while preserving exact native state/event comparison. The initial failure remains recorded; rebuilt probes passed in a second immutable archive with identical application JS/WASM/data hashes. This is a fixture correction, not evidence of an emulator defect.

## Completed correctness acceptance

All 170 compiler tests pass, as do 33 native tests (548 assertions), the 88,035-case expanded focused matrix and 6,915-case unconditional-branch supplement. Both explicit feature modes pass 24,512 native fault comparisons each (49,024 total), including 5,376 new branching-call cases per mode. Missing, wrong and duplicate feature markers are rejected. Both checked standard replays match native for 1,600 images, records and 4,656,051 stereo PCM frames; both 16/32-instruction candidate longer replays match 360 images and audio. The initial fixture-bound failure remains in evidence.

The application archive is unchanged between the original and corrected-probe archives; hashes verify that only test artifacts differ. All performance controls will use the same application bytes where applicable. Normal/live/audio graduation remains pending; completed timing is recorded below.

## Retained contention-affected first batch

The first longer-route batch completed all eight identical-work observations: control32 13.0863/12.9833 s; candidate32 11.1034/11.2068 s; candidate16 11.0222/12.9204 s; untouched baseline 22.9539/14.8035 s. A separate `service_overlap` browser profiling job was observed running concurrently. The entire batch is retained and excluded from promotion comparisons; individual slow samples are not selectively removed. No performance conclusion is drawn from it. Reordered replacement batches wait for the other profiling work to finish and record an external host-process observer; that observer does not rule out all possible host contention or normalize elapsed times.

## Reordered serial timing after observed contention ended

Both routes use identical guest work within each route. The same binary compares broader eligibility at a fixed 32-instruction leaf limit; its 16/32 extended variants separately vary that limit. The untouched post-merge archive is an additional control. All 32 replacement observations are retained; the eight observations affected by known concurrent profiling remain in the preceding section. No guest scheduling or diagnostic counters change in these timing runs.

| Route/batch | Conditional-only, leaf 32 | Expanded, leaf 32 | Expanded, leaf 16 | Untouched merged archive |
| --- | ---: | ---: | ---: | ---: |
| long c | 11.2928s | 11.0973s | 11.5187s | 12.0534s |
| long d | 11.1140s | 11.1097s | 11.0625s | 11.7840s |
| standard c | 11.8136s | 11.4082s | 11.3877s | 12.4067s |
| standard d | 11.1914s | 11.3819s | 11.8284s | 11.7646s |

Means above include every observation. Matching-control pairs are adjacent within each half-batch; the expanded-32 versus archive observations have the expanded-16 variant between them. The host observer detected no concurrent jobs of the watched profiling/test types in these replacement batches. It does not rule out other host load or clock variation, and no times are normalized. Warmup durations, compiled-function counts and allocator footprint are retained; warmup includes startup work and is not an isolated compilation measurement.

- long c: expanded32 versus matching control throughput +1.76%; versus merged archive +8.62%; adjacent eligibility pairs +3.76% / -0.24%.
- long d: expanded32 versus matching control throughput +0.04%; versus merged archive +6.07%; adjacent eligibility pairs +0.43% / -0.34%.
- standard c: expanded32 versus matching control throughput +3.55%; versus merged archive +8.75%; adjacent eligibility pairs -6.61% / +14.59%.
- standard d: expanded32 versus matching control throughput -1.67%; versus merged archive +3.36%; adjacent eligibility pairs -3.98% / +0.77%.

No default change or deployment follows these timings alone. A diagnostic census will quantify removed calls, changing site/length limits and entry-proof fallbacks, including overlap caused only by gaps between actual code snapshots. Live/audio graduation remains outstanding.

## Decision

Do not promote the broader eligibility mask or claim an optimal leaf bound. The fixed-32 marginal lead is +1.76% then +0.04% on the longer route, and +3.55% then -1.67% on standard. Each of the four batches has opposing adjacent eligibility pairs. All means favor expanded32 over the untouched merged archive, but the matching conditional-only control also favors that archive comparison; this does not attribute the combined gain to the newly added operations. The 16/32 choice also changes order across batches. Continue from the measured boundary census, retaining conditional-integer fusion as the preceding candidate and treating these extensions as opt-in research.
