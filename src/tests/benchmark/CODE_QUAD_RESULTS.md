# Four-vector exact code comparison: delivered

The opt-in scanner combines four 16-byte XOR comparisons with OR before testing
for a mismatch. It advances by 64 bytes only when at least that many remain, and
retains the original vector/scalar tail path. Every primary/dependency byte still
participates; mapping, live-entry and address-space checks are unchanged. Equal
mismatch bits in different vectors cannot cancel. The default remains mode 0;
mode 1 is the earlier overlapping-tail experiment and mode 2 selects this one.

The code-span census found that spans of at least 64 bytes represent 21.62% of
calls and 74.02% of requested comparison bytes on the longer route. That motivates
this hypothesis but does not establish a gain. Larger groups can do extra work
on an early mismatch and the mode test has a cost on small comparisons.

## Acceptance

The expanded exact-comparison test covers all three modes, independent unaligned
starts, every-byte mutations through 192 bytes and across 255/256/257 and
511/512/513 boundaries, plus paired mismatches across separate vectors. All 161 compiler tests, 32 native CPU tests (480 assertions) and frontend mode
controls pass. Explicit probe
markers verify original/grouped mode selection in both rebuilt fault matrices,
13,760 exact native comparisons each (27,520 total). An initial frontend launch
failed because the wrapper expected an unavailable Puppeteer Chrome version; its
log is retained. The resumed gate uses the installed Chromium and passes.
Both scanner modes match native for all 1,600 standard images, guest records and
4,919,249 stereo PCM frames. The grouped scanner also matches the 360-image
longer-snake route. These passes establish the tested cases, not general proof.

Raw logs, archive hashes and exact comparisons are in CODE_QUAD_EVIDENCE.json.
The archive is /home/claude/.scratch/eka-benchmark/code-quad-candidate. The ordinary gameplay measurements are recorded below. The scanner remains
opt-in; delivery requires repeatable workload gains and live acceptance.

## Gameplay measurements

All runs execute serially, including warmup, in fresh browsers. Original and
grouped scanners share one application binary; served is the exact live archive.
Each run covers the same 18 guest seconds for its scene, without profiling
counters. Each batch runs its listed order followed by its reverse. All samples
and slow outliers are retained. Requested scanner, TLB and compiler modes, guest
instruction totals and presentations are checked.

| Scene / batch | Original seconds | Grouped seconds | Served seconds | Gain vs original | Gain vs served |
| --- | ---: | ---: | ---: | ---: | ---: |
| long-a | 12.43825 | 12.13650 | 12.25640 | +2.49% | +0.99% |
| long-b | 12.71130 | 12.11285 | 12.30455 | +4.94% | +1.58% |
| standard-a | 12.49160 | 12.27085 | 12.41520 | +1.80% | +1.18% |
| standard-b | 12.50720 | 12.38955 | 12.42400 | +0.95% | +0.28% |

Batch A order: original, grouped, served, served, grouped, original.
Batch B order: grouped, served, original, original, served, grouped.

See the raw rows for individual samples and the acceptance section for
correctness scope. Delivery verification is recorded below.

## Timing assessment

The pooled lead over the exact served build is 1.285% on the longer route and
0.725% on the standard scene. All eight paired same-binary comparisons favor
the grouped scanner; seven of eight comparisons with the served build do. The
closing standard-B candidate loses to its served control (12.3794 versus
12.3444 seconds), despite that batch's positive mean. All 24 observations remain.
The matching-original pooled leads are 3.712% and 1.372%; the former is inflated
by a slow original control. These results support a tiny measured gain on this
host, not a guaranteed percentage or visibly faster gameplay. Live acceptance and actual delivery checks pass as described below.

## Live acceptance before deployment

The unchecked standard replay matches all 1,600 native images, guest records and
4,919,249 stereo PCM frames. Both 120-second live/audio routes apply scanner mode
2 explicitly, sustain realtime, and add no gameplay audio underruns or drops.
Startup recovery events remain in the raw records. The local launcher passes
gesture audio, measured mute/unmute, keyboard/touch, pause/resume, mobile layout
and shutdown. Cache and compiler-policy tests pass. Actual HTTPS delivery and existing-profile upgrade verification also pass;
see below.

## Verified delivery

The live launcher at https://claude-laptop.lan:8188/ selects the archived scanner
mode 2 with compiler policy 7, eager regions 0 and folded TLB index 1. The broader
IR paths remain unselected. The binary default remains scanner mode 0.

The actual trusted HTTPS launcher passes gesture audio, measured mute/unmute,
keyboard/touch, visible pause/resume, mobile layout and shutdown. Downloaded
versioned JS/WASM hashes match the accepted archive. The existing browser profile
from the previous asset-cache acceptance was copied and reused: its first launch
fetches only the new WASM runtime, retaining all 192,004,131 bytes of ROM/RPKG/SIS.
Reload and browser restart then transfer no runtime bodies or game assets. The
requested/applied compiler mode and new manifest WASM hash are explicitly checked.
The original old-build profile is preserved.

WASM SHA256: `bc2af76f19ec38ca8380c9eff84a1766084ba9982f50eec2863e03e7dfbd84aa`

JS SHA256: `5ce9172048cc8fe85cb6ba6451f0cfaa963b87ac67edd19b602904e992fa74ec`

This remains a tiny host-measured performance gain, not a guarantee of visibly
faster play or a fix for all length-dependent work. Every original timing sample
and slow control is retained. Changes and evidence are local; nothing pushed.
