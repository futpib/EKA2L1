# Mode-3 literal comparison

This independently selects runtime literal-PC veneer fusion, feature 128 in the untouched delivered `unsafe-default-candidate` archive (`fba7e89b7`). Mode 3 is explicit; conditional integer leaves, policy 7 and limits 512/16/8/512 remain fixed. The matching control uses feature 0 and lookup 0 in that same archive, which is also the untouched live archive. No extra duplicate baseline is treated as an independent build.

## Correctness

Fresh explicit-policy native fault comparisons: 46016. Standard replay matches all 1,600 images, guest records and 4,656,051 stereo PCM frames; longer replay matches 360 images and 2,832,756 PCM frames. Actual mode, feature and lookup readback are checked. The 175-test compiler suite and control replay acceptance are reused from the same frozen archive, not counted as fresh runs. Intentional runtime code mutation incompatibility remains the accepted mode-3 limitation. Mapping/lifetime, permission, callback and budget checks remain.

## Fixed serial timing plan

Two routes, two batches, 16 observations: A control/candidate/candidate/control, B candidate/control/control/candidate. All observations and passive process/frequency/temperature context remain. Shared audio, physical GPU, identical guest work within each route, no sampling, detailed counters, census or owned overlapping test/build jobs. Startup is separate and is not an isolated compilation metric. No normalization or thermal attribution is performed.

| Route/batch | Control seconds | Candidate seconds | Throughput change | Paired changes |
| --- | ---: | ---: | ---: | --- |
| long-a | 9.13726 | 8.97124 | +1.85% | +1.99% / +1.72% |
| standard-a | 9.10959 | 9.03010 | +0.88% | +0.76% / +1.00% |
| long-b | 9.16643 | 9.01672 | +1.66% | +0.84% / +2.50% |
| standard-b | 9.22058 | 8.95407 | +2.98% | +0.91% / +5.08% |

All 16 observations are retained. These are within-route matching comparisons under mode 3; earlier mode-0 gains or losses are not pooled. Normal/live/audio acceptance and review remain required before any promotion. No source default, service or Git remote changes are made by this experiment.
