# Individual hot-path timings: V29

Same frozen binary, original execution limits, shared faster policy. Each item has 16 serial observations across four routes, with order reversed in the second batch. Coverage-only options and ROM dispatch are disabled. Warmup and complete command costs, allocator footprint and passive host readings are retained. Timing begins only after all owned correctness and diagnostic work is idle.

The user permits a modest loss in one game for a large repeatable gain in the other; zero regression is not required. Realtime remains a separate target. Same-binary comparisons isolate switches, but do not establish the binary-size/layout cost versus the previous baseline or served archive.

Completed items: verification. Retained observations: 18/64.

| Item | Route | Batch | Order | Control s | Candidate s | Throughput change | Candidate realtime | Warmup change s |
|---|---|---:|---|---:|---:|---:|---:|---:|
| verification | sky | 0 | control/candidate | 10.94880 | 11.75130 | -6.83% | 0.511x | +2.510 |
| verification | sky | 1 | candidate/control | 14.17410 | 11.75970 | +20.53% | 0.510x | -1.252 |
| verification | combat | 0 | candidate/control | 12.35910 | 12.35690 | +0.02% | 0.486x | +0.006 |
| verification | combat | 1 | control/candidate | 10.68650 | 12.66350 | -15.61% | 0.474x | +2.521 |
| verification | standard | 0 | control/candidate | 10.20800 | 9.98213 | +2.26% | 1.803x | +2.264 |
| verification | standard | 1 | candidate/control | 10.02940 | 12.17300 | -17.61% | 1.479x | -0.003 |
| verification | long | 0 | candidate/control | 11.39870 | 11.67230 | -2.34% | 1.542x | +2.505 |
| verification | long | 1 | control/candidate | 11.34990 | 9.86746 | +15.02% | 1.824x | -0.241 |

Each batch is one pair per route. Reversals and slow observations remain in the evidence; no cause is inferred from the passive host readings. No trimming or selective reruns. The original panel was stopped prospectively at the user-directed boundary after 18 observations, not completed. Verification has all 16 observations and is mixed/unresolved; the first cache pair is retained without a gain claim. The active browser observation finished normally while only the Python coordinator was suspended to prevent further launches. Its passive host observer has a documented gap; its whole-command duration is unavailable rather than fabricated. Runtime-measured warmup and gameplay clocks are intact. Remaining work uses a smaller declared screen and conditional reversed-order follow-up. Existing correctness acceptance is reused. No deployment or push.
