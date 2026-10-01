# Interactive browser pacing, 2026-10-01

The LAN build kept its original host-clock reference through a browser stall.
When execution resumed, it ran unrestricted until guest time caught up with all
elapsed wall time. Suspending an owned test renderer for six seconds reproduced
8.626 guest seconds in the first 4.821 seconds after resume: 1.789x speed and
37.75 presentations per second, versus approximately 21 during ordinary play.

The interactive worker now rebases its host reference when guest time is more
than 100 ms behind. It discards accumulated wall-clock delay without advancing
guest time or changing timers, instruction accounting, CPU policy or event
ordering. The deterministic, unpaced replay and diagnostic route are unchanged.
Short scheduling delays still catch up. The tolerance matters: rebasing at
20 ms slowed normal play and introduced repeated audio underruns on this host;
100 ms preserved real-time play in the checks below. This is a tested tolerance,
not a claim of an optimal threshold or physical-handset timing accuracy.

## Verification

`src/tests/wasm/pacing.ts` drives the ordinary launcher in Chromium on the NVIDIA
Vulkan renderer. It suspends only that browser's renderer processes for 250 ms
and six seconds, always resumes them during cleanup, and checks guest-time
progress in every sampled one-second window. It also checks average speed,
presentations, input after resume and settled audio. The previous build fails:
its short-stall recovery reaches 1.264x in a one-second window.

With the fix, local fresh play and both recovery phases average 0.998–1.002x;
the highest one-second ratio is 1.040x. Settled audio adds no drops or underruns.
The deliberate process suspensions interrupt audio themselves; those forced
gaps are excluded from the subsequent continuity check.

The separate 120-second manual live session runs at 0.999984x with zero added
audio drops or underruns. Keyboard, touch, mobile layout, mute/unmute and
shutdown pass. Gameplay screenshots were inspected. All 1,600 replay images,
guest records and 4,656,051 stereo PCM frames exactly match the preserved native
reference. Frontend compiler-policy tests also pass.

## Delivery and evidence

The LAN service serves the frozen `pacing-fix-v3-candidate` archive. Downloaded
HTTPS JS/WASM/data/audio hashes match the tested files; WASM SHA-256 is
`ce74aa93f101c1b5739b6d86e4c79cda12302a38b4782289dda5c5e2bc81552a`.
The prior archive is retained. The service's compiler policy is unchanged.
The post-deployment HTTPS regression passes both stalls: six-second recovery
averages 0.999276x, every measured one-second window stays below 1.046x, and
settled audio adds zero drops or underruns. A page reload selects this build.

`PACING_EVIDENCE.json` retains source provenance, runtime policy and asset hashes,
before/after measurements, maximum recovery windows, audio counters, native
comparison results and the post-deployment HTTPS regression. The README includes
commands for rerunning the regression against either a frozen local build or
the LAN launcher. Raw screenshots and samples are under the recorded local
artifact directories.
