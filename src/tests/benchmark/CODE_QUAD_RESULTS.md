# Four-vector exact code comparison experiment

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

No promotion or deployment is implied by this timing record. See the raw rows
for individual samples and the acceptance section for correctness scope.
