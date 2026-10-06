# Dynamic Thumb ROM grouping: rejected screen

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

V30 (75d9f2578) compares mode 0 and mode 3 in the same frozen binary, with the faster shared policy and coverage-only experiments disabled. Four observations trigger the predeclared early-stop rule: one game loses at least 15% without an 8% compensating gain in the other. The remaining reverse-order observations are cancelled prospectively. All results remain; no full acceptance or promotion is claimed.

| Route | Control realtime | Candidate realtime | Single-pair throughput change | Control/candidate warmup |
|---|---:|---:|---:|---:|
| Sky Force moving/firing | 0.528x | 0.177x | -66.4% | 79.10 / 242.63 s |
| Snakes standard | 1.600x | 1.575x | -1.56% | 13.06 / 17.08 s |

Guest instruction and presentation totals agree. The candidate allocates about 49.2 MB more on Sky Force and 47.6 MB more on Snakes at the recorded endpoints; these are allocator readings, not process RSS. Registered functions rise from 10,070 to 11,957 and 12,943 to 13,756 respectively. Static runtime grows 10,388 bytes relative to V29. Startup and gameplay both worsen. Batching module creation alone would not establish a remedy for the gameplay regression; the batching proposal remains parked.

The option stays off. Fresh profiles of the normal-policy control will locate the remaining cost before selecting another change. No deployment or push.
