# Natural snake growth increases the measured workload

Two serial comparisons on the exact served policy-7 archive find higher cost in
the naturally longer-snake scene. Each measures guest seconds 42–60 with rendering
on the physical NVIDIA GPU. Capture, checking, sampling and detailed counters are
off; one browser runs at a time, including warmup, without competing owned heavy
work. All eight observations are retained.

| Batch and order | Short trials, host seconds | Longer trials, host seconds | Short mean | Longer mean | Longer elapsed cost |
| --- | --- | --- | ---: | ---: | ---: |
| A: short, long, long, short | 11.8733, 12.1219 | 13.0875, 13.3501 | 11.9976 | 13.2188 | +10.18% |
| B: long, short, short, long | 11.9432, 12.0135 | 13.6592, 13.2367 | 11.9784 | 13.4480 | +12.27% |

All four adjacent pairs favor the short scene. Both scenes still run faster than
realtime on this laptop: approximately 1.50× for short and 1.34–1.36× for longer,
using batch means. This supports the user's observation of greater cost with
natural growth, but does not reproduce their unspecified device falling behind.

Each route repeats its guest instruction and presentation counts exactly across
all four trials. Short executes 2,620,268,899 instructions with 736 presentations;
longer executes 2,987,830,398 with 720. The longer scene performs **14.03% more
guest instruction work** in the same guest-time window. These are intentionally
different workloads; equal instruction totals across routes are not expected.

## Route and limits of attribution

Both routes use identical installed assets and startup inputs. Enter at 18.64
guest seconds exits the introduction. Short turns right at 18.69 seconds, avoiding
the initial star; longer waits 0.5 seconds to collect it before turning right.
Both choose the overhead camera and subsequently move straight. No game memory,
assets, CPU registers or guest clocks are edited.

Diagnostic exploration shows score 0 with the full short tail visible, versus
score 200 with a much longer tail extending below the screen and reappearing ahead
on the wrapping surface. Both survive past guest second 60 without further pickups
or apparent collisions. Ordinary execution of the served archive reproduces this
contrast without diagnostic stepping. No exact segment count has been extracted.

The routes occupy nearby lanes and differ in score and whether the star remains
visible. This is a natural gameplay comparison, not perfect causal isolation of
segment count. It does not establish physical-phone behavior, a memory leak, or a
particular subsystem as the cause. No slowdown fix or new deployment is claimed.

## Independent native equivalence and frame cadence

Each ordinary served capture matches a fresh native Dynarmic run: all 360 images,
guest records, PCM and audio events. Short has 2,859,665 stereo PCM frames; longer
has 2,878,534. Thus the guest work and image cadence also occur in native emulation.

| Route | Capture span, guest seconds | Distinct images / guest second | Presentations / guest second | Million guest instructions / guest second |
| --- | ---: | ---: | ---: | ---: |
| Short | 17.561387 | 20.4426 | 40.8282 | 145.5737 |
| Longer | 17.953232 | 19.9964 | 39.9928 | 165.9842 |

These captures begin near guest second 42. Their fixed image counts cover different
guest durations, so raw totals are divided by guest time. Capture wall times and
native wall times are not browser headroom measurements.

Native validation used the freshly rebuilt Qt binary. A concurrent long-route
installation launcher failed Xvfb cleanup after reporting successful installation;
the runner rejected it. The failed attempt remains archived. Its complete serial
retry used the unchanged binary and passed.

## Reproduction and evidence

Inputs are snakes-length-short.input and snakes-length-long.input. Raw data lives
under /home/claude/.scratch/eka-benchmark: growth-step-6 and growth-step-7,
growth-short-loop-replay and growth-long-loop-replay, growth-short-native and
growth-long-native-v2, and growth-length-timing-a/b. GROWTH_LENGTH_EVIDENCE.json
records hashes, complete timing observations, reports and native comparisons.

The first trace commit accidentally reused the existing snakes-long.input name.
The dedicated length-route names now hold the exact measured traces; the original
multi-minute input is restored byte-for-byte. Measured input bytes and the served
launcher were unaffected.

Next: diagnostic CPU profiles of both routes to locate the additional work.
Sampling timings will remain separate from these ordinary throughput controls.
