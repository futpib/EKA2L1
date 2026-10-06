# Immutable-ROM module dispatcher serial timing panel

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

All sixteen preplanned same-binary observations are retained, with reversed orders on all four routes. This option panel does not measure total change versus the untouched live archive. Normal browser, detailed counters and sampling disabled, shared audio enabled, mode 3, Thumb memory enabled, ARM exclusive enabled; ARM memory/ROM calls/ROM leaves disabled. Original execution limits retained. Correctness and diagnostic jobs were idle before the first observation.

| Route | Batch | Order (0 control, 1 candidate) | Control s | Candidate s | Throughput change | Candidate realtime |
|---|---:|---|---:|---:|---:|---:|
| sky | 0 | [0, 1] | 11.35610 | 12.77190 | -11.09% | 0.470x |
| sky | 1 | [1, 0] | 12.25040 | 12.90650 | -5.08% | 0.465x |
| combat | 0 | [1, 0] | 10.63200 | 12.05910 | -11.83% | 0.498x |
| combat | 1 | [0, 1] | 10.58850 | 11.05920 | -4.26% | 0.543x |
| standard | 0 | [0, 1] | 12.50750 | 12.44140 | +0.53% | 1.447x |
| standard | 1 | [1, 0] | 11.59360 | 11.57330 | +0.18% | 1.555x |
| long | 0 | [1, 0] | 11.27870 | 11.27380 | +0.04% | 1.597x |
| long | 1 | [0, 1] | 10.90320 | 11.35070 | -3.94% | 1.586x |

Warmup (after the run call returns until the measured route begins; excludes initialization and initial loading, includes guest startup/hot compilation; not an isolated compiler timer):

| Route | Control s | Candidate s |
|---|---:|---:|
| sky | 63.522 | 66.035 |
| sky | 65.785 | 67.295 |
| combat | 90.879 | 94.413 |
| combat | 91.899 | 96.436 |
| standard | 34.896 | 32.129 |
| standard | 33.660 | 33.902 |
| long | 46.452 | 47.203 |
| long | 46.202 | 46.696 |

Whole benchmark command (browser setup, initialization, guest warmup, measurement and export; not gameplay throughput):

| Route | Control s | Candidate s |
|---|---:|---:|
| sky | 81.322 | 84.383 |
| sky | 84.379 | 86.634 |
| combat | 107.970 | 112.836 |
| combat | 108.875 | 113.839 |
| standard | 54.068 | 51.061 |
| standard | 51.716 | 51.965 |
| long | 64.341 | 64.941 |
| long | 63.536 | 64.692 |

Final runtime footprint (WASM allocator bytes, excluding browser and JIT code memory; compiled_functions counts cumulative installations, including replaced functions, not live table slots):

| Route | Control cumulative installations | Candidate cumulative installations | Control allocated bytes | Candidate allocated bytes |
|---|---:|---:|---:|---:|
| sky | 89083 | 89083 | 585345272 | 586135664 |
| sky | 89083 | 89083 | 585345640 | 586135280 |
| combat | 90269 | 90269 | 594455816 | 595246088 |
| combat | 90269 | 90269 | 594455720 | 595246424 |
| standard | 103716 | 103716 | 583571680 | 584362328 |
| standard | 103716 | 103716 | 583570864 | 584360920 |
| long | 104413 | 104413 | 592549128 | 593339208 |
| long | 104413 | 104413 | 592548968 | 593339592 |

Guest instruction, presentation and guest-time totals match within every pair. Actual option readback is asserted in each run. The panel compares the module-local dispatcher (off versus on), with first-use compilation policy 3 in both in one frozen V27b archive, with compiled SVC enabled in both configurations; it is not a total-change comparison with the untouched live build.

Coverage and speed are separate results. Assess all eight pairs before promotion; this panel alone does not measure total change against the untouched live archive. Preserve all observations. Live remains unchanged and nothing is pushed.

## Decision

Do not promote: both Sky Force pairs on both routes regress (stationary -11.09%/-5.08%, combat -11.83%/-4.26%), despite the separate census's ~16% outer-call reduction. Standard Snakes is nearly flat (+0.53%/+0.18%); longer is +0.04%/-3.94%. The option remains disabled by default. Next investigate bounded state retention across connected regions; repeated native-correct gameplay gains remain mandatory. Zero interpretation is not a promotion gate that overrides speed.
