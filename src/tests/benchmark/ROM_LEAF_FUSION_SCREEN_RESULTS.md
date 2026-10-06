# ROM policy screen

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

All planned serial observations are retained, with original limits, mode 3,
literal feature 128, hardware GPU, shared audio, no capture, no sampling and
no detailed counters. Guest instruction and presentation totals match within
each route. Each archive is frozen; recorded harness HEAD is not build provenance.

| Route | Basic ROM seconds | Regions seconds | Regions + leaves seconds | Leaves vs basic |
| --- | ---: | ---: | ---: | ---: |
| sky | 11.0384 | 11.9580 | 10.6451 | +3.69% |
| combat | 11.4426 | 10.6352 | 10.2098 | +12.07% |
| standard | 10.6304 | 10.2274 | 10.3369 | +2.84% |
| long | 11.3606 | 11.2114 | 11.7901 | -3.64% |

One observation per policy per route is exploratory. ROM leaf fusion improves
the Sky Force and standard Snakes comparisons versus basic ROM, but longer
Snakes is slower. It does not support a dependable shared gain or promotion.
The option remains off. All twelve observations, including the slower long
route, remain in the evidence.
