# Unsafe code-mutation cost attribution

This follows the completed fully unsafe / matching-control / untouched-live comparison in UNSAFE_CODE_RESULTS.md. The same frozen archive is used; no compiler, scheduling, source/leaf/site/runner limit or guest-work change is introduced.

Modes: 0 exact control; 1 scan removal only; 2 generated code-write guard and entry-proof code-overlap removal only; 3 both. The archive has version, epoch and write-protection options disabled in every mode. These are deliberately unsafe diagnostics, OFF by default. No deployment or push.

Modes 1 and 2 each pass 40,640 explicitly selected native fault comparisons and exact standard (1,600 images) / longer (360 images) native image, guest-record and PCM replays. Actual-mode readback, correct capture starts and verify_aot=0 are checked. The 175-test suite and mode 0/3 acceptance are reused from the same archive; they are not fresh runs. Intentional stale-code counterexamples remain documented in the full experiment. Replay does not prove no code mutations occur.

The fixed serial plan contains 32 observations: four same-binary modes, both routes, two orders followed by their reversals. Batch A is control/scan/guards/unsafe and reverse; B is guards/unsafe/control/scan and reverse. Thus each mode moves between inner and outer positions. All observations and passive host readings are retained. The untouched live archive was measured in the preceding full-mode comparison, not this attribution panel. Performance counters are disabled during timing; zero counter fields are not mutation or invalidation evidence. Component changes need not be additive because emitted code and browser optimization can interact.


## Serial timings

Seconds for identical guest work within each route, lower is faster. All 32 samples remain.

| Route/batch | Exact | Scan removal | Guard removal | Both |
| --- | ---: | ---: | ---: | ---: |
| long a | 11.2355 | 9.4435 | 12.0050 | 9.4709 |
| long b | 11.1985 | 9.4906 | 11.0408 | 9.5538 |
| standard a | 11.2226 | 10.8785 | 11.9951 | 9.2147 |
| standard b | 11.3261 | 9.5162 | 11.3611 | 10.3216 |


long a: scan +18.98% throughput versus exact, corresponding-half pairs +17.84% / +20.14%; guards -6.41% throughput versus exact, corresponding-half pairs -6.92% / -5.89%; unsafe +18.63% throughput versus exact, corresponding-half pairs +13.06% / +24.78%; 

long b: scan +18.00% throughput versus exact, corresponding-half pairs +17.76% / +18.23%; guards +1.43% throughput versus exact, corresponding-half pairs +2.92% / -0.02%; unsafe +17.22% throughput versus exact, corresponding-half pairs +23.27% / +11.75%; 

standard a: scan +3.16% throughput versus exact, corresponding-half pairs -0.34% / +6.88%; guards -6.44% throughput versus exact, corresponding-half pairs -4.78% / -8.02%; unsafe +21.79% throughput versus exact, corresponding-half pairs +21.45% / +22.13%; 

standard b: scan +19.02% throughput versus exact, corresponding-half pairs +21.43% / +16.71%; guards -0.31% throughput versus exact, corresponding-half pairs +5.39% / -5.40%; unsafe +9.73% throughput versus exact, corresponding-half pairs +5.41% / +14.40%; 

Corresponding-half pairs are not all adjacent. No samples are filtered or normalized; host readings do not establish causes of individual slow observations. Interpret the per-batch and paired results before pooled averages. This experiment deliberately breaks self-modifying-code semantics and is not a deployable optimization.

## Completed interpretation

The full-mode panel (24 observations) and component panel (32 observations) are complete. Every sample is retained. All eight full-unsafe matching-control batch means favor removing scans and guards; 15 of 16 corresponding-half pairs do. The original panel includes slow candidate and control observations and a reversed longer-route pair, so it remains part of the conclusion rather than being replaced by the cleaner component panel. This establishes material cost on the tested gameplay windows, not a stable universal percentage or proof of immutable guest code.

Scan removal is the repeatable component: longer-route throughput gains are 18.98% and 18.00%; standard gains are 3.16% and 19.02%. Seven of eight corresponding-half pairs favor scan removal. Guard-only changes are -6.41% / +1.43% on longer and -6.44% / -0.31% on standard; its positive longer batch also has opposing pairs. Removing guards in addition to scans loses three of four batch means. The isolated code-write checks therefore have no demonstrated repeatable net benefit to remove in this study. Do not add the two percentages or assign a fixed cost to those checks.

Incremental guard removal on the scan-free path (throughput change, both versus scan-only):

- long-a: -0.29%
- long-b: -0.66%
- standard-a: +18.06%
- standard-b: -7.80%

The measured target for later code-lifecycle work is avoiding repeated instruction-byte validation while retaining required semantics. No safe replacement has been designed or substituted in this experiment. Ordinary translation, mapping/address-space/lifetime handling, permissions, fault/callback behavior, budgets, interrupts and guest scheduling remain fixed.

Validation comprises the 175-test compiler suite, 40,640 explicitly selected native fault comparisons for each of modes 0/1/2/3, and exact standard/longer native image, guest-record and PCM replays in each mode. The total is 162,560 policy-selected fault comparisons; the base cases are reused across policies, not independent new inputs. Disassembly and deliberately stale primary/dependency/overlapping-store counterexamples verify the removed work and intentional semantic incompatibility. Build-time version/epoch/write-protection options were OFF in the tested archive; optional tracking builds were not tested.

Default mode remains 0. The live conditional-only archive was neither replaced nor configured unsafe. No deployment or push. Runner diagnostics and literal-PC work were preserved while this priority experiment ran and may now resume serially from their existing checkpoints.
