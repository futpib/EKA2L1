# Proved read-span experiment

Rejected: a complete serial batch averages **14.45055s** for the candidate,
**13.49870s** for its immediate direct-loop baseline, and **13.96305s** for the
served ADD/ADC build. Throughput changes are **-6.59%** and **-3.37%** respectively.
The first candidate run was close to baseline; the second was much slower.
Every run is retained. This batch does not establish a gain; no confirmation,
live acceptance or deployment was attempted. It does not disprove other memory
data-flow optimizations.

| Order | Build | Seconds |
| --- | --- | ---: |
| 1 | Direct-loop baseline | 13.4494 |
| 2 | Read spans | 13.4003 |
| 3 | Served | 13.6428 |
| 4 | Served | 14.2833 |
| 5 | Read spans | 15.5008 |
| 6 | Direct-loop baseline | 13.5480 |

Physical GPU and shared audio; guest seconds 78–96, 3,975,618,624 instructions,
676 presentations per run. Warmup and runs were serial with no owned build,
test or profiling overlap. Capture, sampled checking and detailed counters were
disabled for timing. Equal replication and symmetric order do not eliminate
background variation or establish statistical significance.

## Implementation and exact exits

A conservative data-flow pass groups unconditional immediate word LDRs by
unchanged base-register value within a basic block. The first access proves the
entire aligned same-page read span, including permission, endian and nonzero
TLB tag/base. Later accesses load directly from the proved host span, in their
original order. Failed proofs leave before the current instruction for normal
runner fallback; they never raise a future fault early. Instruction budgets,
AOT_EXIT, code validation and callback handling remain unchanged.

Definitions end the affected base version. Labels, conditionals, branches,
stores, unknown encodings and inlined leaves end groups. The analyzed subset
includes ordinary ALU, long multiply and non-writeback LDM. This is a small
address-proof pass, not a general SSA compiler. No guest address whitelist or
whole-game coverage percentage is claimed. The runtime patch is preserved as
read_spans_experiment.patch and reverted; independent tests remain.

## Verification

All 138 WASM tests pass, including 13,824 new exact state/memory/budget fixtures
covering clobbers, joins, loops, conditional barriers, aliases, permissions,
endian, page boundaries and address wrap. All three native CTest targets and
seven frontend checks pass. Four explicitly rebuilt 672-case fault modes match
native. A checked replay matches 1,600 images, guest records and 4,919,249 stereo
PCM frames exactly. The existing native-identical movement heuristic failure
remains, and is not described as passing.

A new production-runner `--read-spans` fault mode adds 48 fixtures: MOVS then two
word loads, with a valid first load and potentially faulting second load. It
covers mapped/unmapped memory, page crossing, endian and all four handler
policies, comparing callback-visible registers/flags, event order, instruction
count and exact memory against native Dynarmic. All 48 match. Its probes were
explicitly rebuilt and archived separately with source/probe hashes; the
original candidate archive was not overwritten. Tests are committed separately
as 0d0508fe6 and do not depend on keeping the optimization.

## Reproduction

READ_SPANS_EVIDENCE.json embeds measurements, native comparisons and source/hash
records. Candidate archive: /home/claude/.scratch/eka-benchmark/read-spans-candidate,
based on be6c05ded plus its saved patch. WASM SHA-256:
81ec073ad5a80463ca105ebc460f8c036f8526e6d959f9542f151b57c2372bfc.
Use serial_variants.py with EKA2L1_SHARED_AUDIO=1, the recorded archive paths and
forward/reverse order. For the runner probe use --read-spans on both explicitly
built targets, then compare_cpu_faults.py with --cases 48 --require-equal.
The served archive remains addv-candidate; no new speedup is delivered.
