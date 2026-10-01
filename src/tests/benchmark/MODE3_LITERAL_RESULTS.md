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

## Normal live acceptance

All eight timing pairs favor the candidate. The gain is modest and variable; the larger standard confirmation includes a slower control and faster closing candidate. Both two-minute live/audio routes sustain realtime with zero added measured underruns or drops. Desktop/mobile controls and gesture audio pass with actual mode 3 and feature 128 readback. The known native-matching level restart remains; no uninterrupted-gameplay claim is made. Actual HTTPS and existing-profile policy-only upgrade checks follow.

## Verified LAN selection

Feature 128 is selected at https://claude-laptop.lan:8188/ after reload. The service still leaves EKA2L1_UNSAFE_CODE unset, selecting normal default 3. Application/archive hashes are unchanged; this is a policy update. Explicit feature 0 and mode 0 remain selectable. Actual HTTPS controls, gesture audio, keyboard/touch, pause/resume and mobile layout pass, with direct mode-3 and feature-128 readback. Screenshots were inspected.

The existing mode-3 browser profile reuses all 192,004,131 ROM/game bytes and transfers zero runtime bytes on policy upgrade, reload and browser restart. The new explicit policy-only cache harness requires unchanged runtime hashes and a changed compiler policy; ordinary binary-upgrade checks remain intact.

Both live routes have zero added measured audio underruns/drops. Maximum sampled lag (manual / automatic) is 45.90 ms / 16.06 ms. The known native-matching level restart remains. Timing gains are specific to the two measured routes and retain all slow controls and candidates; no general emulator speed percentage is implied.

The next independent lookup-layout experiment retains feature 128 in both modes. Existing optimization work continues. No Git push.

## Mode-3 boundary counts

Separate interpreter-checked diagnostic runs pass on both routes with stride 1024. Their cache clears and extra interpreter decoding perturb compiled-region formation, so their counts are retained as checked diagnostics and are not compared with the normal control. Two subsequent runs disable checking to match the original detailed control profiles. Detailed counters are enabled in these runs; their elapsed time is not acceptance timing. Guest instruction totals and presentations remain identical within each route.

| Route | Control compiled invocations | Literal-PC invocations | Removed | Control/candidate compiled guest instructions |
| --- | ---: | ---: | ---: | --- |
| long | 140,994,669 | 131,883,365 | 9,111,304 | 3,027,181,324 / 3,026,988,987 |
| standard | 140,626,048 | 131,572,108 | 9,053,940 | 2,972,237,245 / 2,972,188,093 |

Both normal modes record zero code-byte/version checks. Removed region invocations are not a count of individual register stores or loads. The prior mode-0 module-size measurements are not relabeled as mode-3 evidence. This records the runtime mechanism alongside the 16 accepted mode-3 timing observations, without pooling either diagnostic wall time or older policy results.
