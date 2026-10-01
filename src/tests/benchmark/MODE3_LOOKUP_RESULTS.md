# Mode-3 lookup comparison

This independently selects the existing outlined recent-entry cache lookup, layout 1 in the untouched delivered `unsafe-default-candidate` archive (`fba7e89b7`). Mode 3 is explicit; conditional integer leaves, policy 7 and limits 512/16/8/512 remain fixed. The matching control uses lookup 0 and feature 128, matching the delivered literal-veneer policy in that same archive, which is also the untouched live archive. No extra duplicate baseline is treated as an independent build.

## Correctness

Fresh explicit-policy native fault comparisons: 46016. Standard replay matches all 1,600 images, guest records and 4,656,051 stereo PCM frames; longer replay matches 360 images and 2,832,756 PCM frames. Actual mode, feature and lookup readback are checked. The 175-test compiler suite and control replay acceptance are reused from the same frozen archive, not counted as fresh runs. Intentional runtime code mutation incompatibility remains the accepted mode-3 limitation. Mapping/lifetime, permission, callback and budget checks remain.

## Fixed serial timing plan

Two routes, two batches, 16 observations: A control/candidate/candidate/control, B candidate/control/control/candidate. All observations and passive process/frequency/temperature context remain. Shared audio, physical GPU, identical guest work within each route, no sampling, detailed counters, census or owned overlapping test/build jobs. Startup is separate and is not an isolated compilation metric. No normalization or thermal attribution is performed.

| Route/batch | Control seconds | Candidate seconds | Throughput change | Paired changes |
| --- | ---: | ---: | ---: | --- |
| long-a | 9.08774 | 11.83154 | -23.19% | -37.99% / +1.05% |
| standard-a | 16.33142 | 10.34971 | +57.80% | -6.39% / +115.65% |
| long-b | 14.82750 | 10.80561 | +37.22% | +98.73% / -12.06% |
| standard-b | 16.40735 | 10.66733 | +53.81% | +72.19% / +28.85% |

All 16 observations are retained. These are within-route matching comparisons under mode 3; earlier mode-0 gains or losses are not pooled. Normal/live/audio acceptance and review remain required before any promotion. No source default, service or Git remote changes are made by this experiment.
