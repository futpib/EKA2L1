# Three-access direct-memory span proofs

Not adopted. Snakes has no repeatable CPU gain; Sky Force uses 2.44% more
worker CPU and is slower in both pairs. The implementation and regression
tests are preserved in the evidence file; production behavior is restored.

This follows the [two-access experiment](DIRECT_SPAN_RESULTS.md). The production
candidate differs from that implementation only in raising the minimum from
two to three qualifying memory instructions. Affine address analysis, entry
guards, larger arena spans and fallback behavior are identical. TLB's threshold
remains four. This counts static qualifying instructions, so conditionals and
short budgets can still execute fewer accesses at runtime.

The comparison is against the restored production baseline, with both builds
measured again in this campaign. It is not a simultaneous two-versus-three
threshold comparison. Baseline source is `5a4920c9b`, whose production code and
browser artifacts are identical to `e7fd4ab28` used by the preceding experiment.

## Correctness

- 1,450 span state/guard cases pass, including explicit rejection of two-access
  blocks and successful selection of three-access affine blocks. Positive
  fixtures add a third access; rejection fixtures also contain enough accesses
  to distinguish unsafe address relations from merely missing the threshold.
- Existing checks pass: 816 ARM/Thumb memory comparisons, direct mapping
  publication and callback lifetime, 21,146 ARM short-block cases, cached callback
  state and 24,192 instruction-budget cases.
- Snakes and Sky Force each match 60 native reference frames exactly, including
  pixels, guest timestamps, instruction counts, PCM and audio events. These
  correctness replays use SwiftShader; CPU timings use the hardware GPU.

Rebuilding the restored production source reproduces all six baseline browser
artifacts byte-for-byte. The preserved candidate passes `git apply --check`
against that source.

## CPU measurements

Positive change means slower. Observations are worker CPU seconds; pairs
compare forward and reversed run order separately.

| # | Game | Baseline CPU observations | Candidate CPU observations | Mean CPU change | Pair changes |
|---|---|---|---|---:|---|
| 1 | Snakes | 1.559268, 1.781791 | 1.680219, 1.603881 | -1.70% | +7.76%, -9.98% |
| 2 | Sky Force combat | 8.341634, 8.495607 | 8.432974, 8.815634 | +2.44% | +1.09%, +3.77% |

Snakes' opposing pair results make its average improvement inconclusive.
Sky Force regresses in both pairs. Raising the threshold to three therefore
does not establish a useful gain over production. The earlier two-access
result came from a separate campaign; these measurements do not isolate the
performance difference between thresholds two and three or identify the
regression's cause. No adoption or LAN deployment follows from this experiment.

As in the preceding experiment, each game uses baseline-candidate-candidate-
baseline order with two observations per build, fresh browsers, the direct
backend, hardware GPU and shared audio. Sampling, tracing and custom diagnostics
are off. No owned build, compiler test or correctness replay overlaps timing.
Worker scheduler CPU seconds are primary; renderer CPU and wall time are also
retained. All completed observations are kept.

Snakes uses guest time 21-25 seconds; Sky Force uses 42.000001-48 seconds. The
driver checks equal instruction endpoints and presentation journals across
builds: 644,728,231 and 2,171,043,925 instructions, respectively. Two observations
per build are a directional screen on a shared host, not a precise estimate of
a small performance difference.

## Evidence and reproduction

[Complete evidence](DIRECT_SPAN_THREE_RESULTS.json) records the source patch,
build hashes, commands, focused test output, native-reference hashes, every CPU
observation and the timing driver. Build and freeze the baseline, apply the
patch, build and freeze the candidate, run the recorded correctness checks,
then run the recorded paired campaign. The commands use the existing
`memory_implementations.py` driver with direct mode 2 and explicit guest windows
of 4,000,000 and 6,000,000 microseconds. Local artifacts are under
`/home/claude/.scratch/eka-direct-span-three/`.
