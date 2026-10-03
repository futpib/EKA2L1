# Thumb memory timing: first verified stage

This is the historical v2 panel. Direct Thumb memory is now the browser default;
see the [policy-17 measurements and live verification](THUMB_MEMORY_DEFAULT_RESULTS.md).
The results below describe the older binary and are not pooled with that panel.

The fixed 24-observation serial panel supports a modest Sky Force improvement,
not the realtime target. All four matching Sky Force pairs favor direct Thumb
memory. Six guest seconds still take about 14 seconds. Snakes retains unpaced
realtime headroom, but mixed pairs do not establish zero slowdown.

| # | Route | Order | Control mean (s) | Candidate mean (s) | Throughput change | Candidate realtime capacity |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 1 | Sky Force, 6 guest seconds | ABBA | 15.82810 | 14.61185 | +8.32% | 0.411x |
| 2 | Sky Force, 6 guest seconds | BAAB | 15.63220 | 13.96415 | +11.95% | 0.430x |
| 3 | Snakes standard, 18 guest seconds | ABBA | 10.40310 | 10.46900 | -0.63% | 1.719x |
| 4 | Snakes standard, 18 guest seconds | BAAB | 11.28150 | 10.41865 | +8.28% | 1.728x |
| 5 | Snakes longer, 18 guest seconds | ABBA | 10.86390 | 9.92977 | +9.41% | 1.813x |
| 6 | Snakes longer, 18 guest seconds | BAAB | 9.89959 | 9.85901 | +0.41% | 1.826x |

A is the original callback policy, B is the new direct-memory policy. Both use
the same untouched v2 archive, with static WASM digest
`548fe0ca861a1a2898aa51d1c676961301406752f3ec68225b9bb4404758394d`.
Implementation and correctness evidence are committed in `2e4f1f0d4`.

The second standard batch includes a 12.2885-second control; that slow observation
inflates the positive mean. Its other pair favors the control by 4.35%. The first
standard batch also has opposing pairs. The second longer batch has opposing
pairs (+1.66% and -0.82%). All samples, orders, warmup times, actual policy reports
and passive host observations are retained. The observer does not establish the
cause of variability. No sample was rejected, replaced, normalized or pooled with
instrumented diagnostics or the earlier no-shared-audio screens.

Each route has identical guest work in all eight observations:

- Sky Force: 6 seconds, 2,142,147,961 instructions, 192 presentations.
- Snakes standard: 18 seconds, 2,964,235,296 instructions, 381 presentations.
- Snakes longer: 18 seconds, 3,031,637,220 instructions, 380 presentations.

Each fresh browser completes its warmup and timed window before the next starts.
The panel uses physical NVIDIA rendering without readback/capture, shared audio,
no CPU sampling or detailed counters, mode 3, literal feature 128, lookup 0,
eager ARM regions 0, and unchanged limits `512,16,8,512`. No owned compiler build,
acceptance suite or profiler competed with these measurements. Other system work
was not terminated. The source commit changed from the uncommitted snapshot to
its verified checkpoint during the panel; the binary archive and harness stayed
identical, and each report retains its observed Git metadata.

This was only a same-binary policy comparison. At this checkpoint, new-binary
cost versus the untouched live archive, sustained normal live/audio tests and
final promotion remained open. The option stayed off and the served archive was
unchanged. Subsequent work kept Thumb PC/runtime fields in the existing state
cache, with callback publication/reload and exact budget behavior tested before
measurement.
