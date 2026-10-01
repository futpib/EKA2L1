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
