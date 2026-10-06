# Reassessment of optimization timing decisions

The previous uncontrolled-clock measurements do not establish that small gains
or regressions came from an optimization. This campaign reopens those verdicts
using frozen builds, identical guest work, measured CPU frequency and a reserved
physical core. It includes recent changes and archived candidates that had
passed correctness but were rejected or left unresolved on noisy timing evidence.

[Live result table](CONTROLLED_RESULTS.md) and [measurement method](CONTROLLED_BENCHMARKS.md).

## Scope

The five fixed plans contain 65 candidate/control comparisons, comprising 103
game comparisons and 824 valid observations. Each game comparison uses ABBA
then BAAB. Historical Snakes-only experiments remain explicitly Snakes-only;
their results are not presented as Sky Force measurements. No production
optimization or default is changed by this task.

| # | Phase | Comparison | Original report |
| ---: | --- | --- | --- |
| 1 | isolated | compact-dispatch | [DISPATCH_AND_DIVISION_RESULTS](DISPATCH_AND_DIVISION_RESULTS.md) |
| 2 | isolated | division-digits | [DISPATCH_AND_DIVISION_RESULTS](DISPATCH_AND_DIVISION_RESULTS.md) |
| 3 | isolated | state-pruning | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 4 | isolated | trusted-lookup-inline | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 5 | isolated | entry-only-pruning | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 6 | isolated | store-only-pruning | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 7 | isolated | direct-span-two | [DIRECT_SPAN_RESULTS](DIRECT_SPAN_RESULTS.md) |
| 8 | isolated | direct-span-three | [DIRECT_SPAN_THREE_RESULTS](DIRECT_SPAN_THREE_RESULTS.md) |
| 9 | isolated | thumb-static | [THUMB_STATIC_REGION_RESULTS](THUMB_STATIC_REGION_RESULTS.md) |
| 10 | isolated | thumb-transfer-gate | [THUMB_TRANSFER_GATE_RESULTS](THUMB_TRANSFER_GATE_RESULTS.md) |
| 11 | isolated | batched-counts | [BATCHED_INSTRUCTION_COUNTS](BATCHED_INSTRUCTION_COUNTS.md) |
| 12 | isolated | span-page-reuse | [SPAN_PAGE_REUSE_RESULTS](SPAN_PAGE_REUSE_RESULTS.md) |
| 13 | architecture | dynamic-rom-cohorts | [DYNAMIC_ROM_COHORT_TIMING_RESULTS](DYNAMIC_ROM_COHORT_TIMING_RESULTS.md) |
| 14 | architecture | sparse-rom-lookup | [SPARSE_ROM_LOOKUP_TIMING_RESULTS](SPARSE_ROM_LOOKUP_TIMING_RESULTS.md) |
| 15 | architecture | compiled-syscalls | [COMPILED_SVC_TIMING_RESULTS](COMPILED_SVC_TIMING_RESULTS.md) |
| 16 | architecture | compiled-memory-misses | [COMPILED_MEMORY_MISSES_TIMING_RESULTS](COMPILED_MEMORY_MISSES_TIMING_RESULTS.md) |
| 17 | architecture | ram-first-use | [RAM_FIRST_USE_COMPILATION_TIMING_RESULTS](RAM_FIRST_USE_COMPILATION_TIMING_RESULTS.md) |
| 18 | architecture | rom-first-use-recycling | [ROM_FIRST_USE_RECYCLING_TIMING_RESULTS](ROM_FIRST_USE_RECYCLING_TIMING_RESULTS.md) |
| 19 | architecture | rom-module-dispatch | [ROM_MODULE_DISPATCH_TIMING_RESULTS](ROM_MODULE_DISPATCH_TIMING_RESULTS.md) |
| 20 | architecture | rom-state-cohorts | [ROM_STATE_COHORT_TOTAL_TIMING_RESULTS](ROM_STATE_COHORT_TOTAL_TIMING_RESULTS.md) |
| 21 | architecture | mixed-ir | [IR_STACK_VALUES_RESULTS](IR_STACK_VALUES_RESULTS.md) |
| 22 | architecture | longer-ir-segments | [IR_STACK_VALUES_RESULTS](IR_STACK_VALUES_RESULTS.md) |
| 23 | architecture | ir-stack-values | [IR_STACK_VALUES_RESULTS](IR_STACK_VALUES_RESULTS.md) |
| 24 | extended | quiet-runtime-cuts | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 25 | extended | ram-hit-inline | [PROFILE_OPTIMIZATION_PLATEAU_RESULTS](PROFILE_OPTIMIZATION_PLATEAU_RESULTS.md) |
| 26 | extended | verifier-specialization | [PROFILE_OPTIMIZATION_PLATEAU_RESULTS](PROFILE_OPTIMIZATION_PLATEAU_RESULTS.md) |
| 27 | extended | guard-publication-omission | [PROFILE_OPTIMIZATION_PLATEAU_RESULTS](PROFILE_OPTIMIZATION_PLATEAU_RESULTS.md) |
| 28 | extended | conditional-loop-budgets | [LOOP_BUDGET_RESULTS](LOOP_BUDGET_RESULTS.md) |
| 29 | extended | broad-loop-budgets | [LOOP_BUDGET_RESULTS](LOOP_BUDGET_RESULTS.md) |
| 30 | extended | lazy-flags | [FLAG_DATAFLOW_RESULTS](FLAG_DATAFLOW_RESULTS.md) |
| 31 | extended | incoming-register-definitions | [FLAG_DATAFLOW_RESULTS](FLAG_DATAFLOW_RESULTS.md) |
| 32 | extended | wide-result-reuse | [WIDE_REUSE_RESULTS](WIDE_REUSE_RESULTS.md) |
| 33 | extended | direct-register-stores | [REGISTER_STORE_RESULTS](REGISTER_STORE_RESULTS.md) |
| 34 | extended | conditional-alu-select | [CONDITIONAL_ALU_RESULTS](CONDITIONAL_ALU_RESULTS.md) |
| 35 | extended | compare-operand-reuse | [COMPARE_CONDITION_RESULTS](COMPARE_CONDITION_RESULTS.md) |
| 36 | extended | whole-entry-budget | [ENTRY_BUDGET_RESULTS](ENTRY_BUDGET_RESULTS.md) |
| 37 | extended | outlined-entry-budget | [OUTLINED_BUDGET_RESULTS](OUTLINED_BUDGET_RESULTS.md) |
| 38 | extended | cached-address-displacement | [RUNNER_SPECIALIZATION_RESULTS](RUNNER_SPECIALIZATION_RESULTS.md) |
| 39 | extended | deferred-read-exit | [RUNNER_SPECIALIZATION_RESULTS](RUNNER_SPECIALIZATION_RESULTS.md) |
| 40 | extended | owner-core-reuse | [LOOKUP_FOLLOWUP_RESULTS](LOOKUP_FOLLOWUP_RESULTS.md) |
| 41 | extended | aligned-cache-hash | [LOOKUP_FOLLOWUP_RESULTS](LOOKUP_FOLLOWUP_RESULTS.md) |
| 42 | inlining | expanded-leaf-eligibility | [EXPANDED_LEAVES_RESULTS](EXPANDED_LEAVES_RESULTS.md) |
| 43 | inlining | call-prefix-fusion | [CALL_PREFIX_RESULTS](CALL_PREFIX_RESULTS.md) |
| 44 | inlining | preserve-inner-leaves | [PRESERVE_INNER_RESULTS](PRESERVE_INNER_RESULTS.md) |
| 45 | inlining | branch-veneer-fusion | [BRANCH_VENEER_RESULTS](BRANCH_VENEER_RESULTS.md) |
| 46 | inlining | tail-prefix-fusion | [TAIL_PREFIX_RESULTS](TAIL_PREFIX_RESULTS.md) |
| 47 | inlining | expanded-leaf-bound | [EXPANDED_LEAVES_RESULTS](EXPANDED_LEAVES_RESULTS.md) |
| 48 | inlining | conditional-leaf-bound | [REGION_LIMITS_RESULTS](REGION_LIMITS_RESULTS.md) |
| 49 | inlining | hot-source-window | [REGION_LIMITS_RESULTS](REGION_LIMITS_RESULTS.md) |
| 50 | inlining | four-inline-sites | [CONDITIONAL_SITE_LIMITS_RESULTS](CONDITIONAL_SITE_LIMITS_RESULTS.md) |
| 51 | inlining | sixteen-inline-sites | [CONDITIONAL_SITE_LIMITS_RESULTS](CONDITIONAL_SITE_LIMITS_RESULTS.md) |
| 52 | inlining | shorter-region-chain | [CONDITIONAL_RUNNER_LIMITS_RESULTS](CONDITIONAL_RUNNER_LIMITS_RESULTS.md) |
| 53 | inlining | uncapped-region-chain | [CONDITIONAL_RUNNER_LIMITS_RESULTS](CONDITIONAL_RUNNER_LIMITS_RESULTS.md) |
| 54 | inlining | eager-rom-regions | [ROM_LEAF_FUSION_SCREEN_RESULTS](ROM_LEAF_FUSION_SCREEN_RESULTS.md) |
| 55 | inlining | rom-leaf-fusion | [ROM_LEAF_FUSION_SCREEN_RESULTS](ROM_LEAF_FUSION_SCREEN_RESULTS.md) |
| 56 | inlining | bounded-rom-calls | [ROM_BOUNDED_CALL_SCREEN_RESULTS](ROM_BOUNDED_CALL_SCREEN_RESULTS.md) |
| 57 | memory | allocation-ranges | [MEMORY_IMPLEMENTATIONS_RESULTS](MEMORY_IMPLEMENTATIONS_RESULTS.md) |
| 58 | memory | full-page-table | [MEMORY_IMPLEMENTATIONS_RESULTS](MEMORY_IMPLEMENTATIONS_RESULTS.md) |
| 59 | memory | last-page-only | [MEMORY_CACHE_RESULTS](MEMORY_CACHE_RESULTS.md) |
| 60 | memory | folded-tlb | [MEMORY_CACHE_RESULTS](MEMORY_CACHE_RESULTS.md) |
| 61 | memory | tlb-plus-last-page | [MEMORY_CACHE_RESULTS](MEMORY_CACHE_RESULTS.md) |
| 62 | memory | folded-tlb-plus-last-page | [MEMORY_CACHE_RESULTS](MEMORY_CACHE_RESULTS.md) |
| 63 | memory | older-page-cache-guards | [MEMORY_CACHE_RESULTS](MEMORY_CACHE_RESULTS.md) |
| 64 | memory | page-cache-span-reuse | [MEMORY_CACHE_RESULTS](MEMORY_CACHE_RESULTS.md) |
| 65 | memory | current-tlb-vs-direct | [DIRECT_ACCESS_LOWERING_RESULTS](DIRECT_ACCESS_LOWERING_RESULTS.md) |

## What these measurements can establish

Recent frozen builds answer the marginal runtime question at the same source
stage where each decision was made. Archived compiler, dispatch and inlining
experiments answer the corresponding historical question. They do not prove
the same change wins on the current direct-memory/default compiler combination.
A recovered historical win is a reason to port and measure that change again,
not to add its percentage to current throughput.

Warmed guest-worker CPU time is the main metric. Wall time, native retired
instructions, cycles, actual frequency and all four adjacent pair changes
remain visible. Warmup, build time and offline compiler probes are excluded.
A lower native instruction count is not treated as a sufficient speed verdict.
Four observations per variant describe repeatability here; they are not a
precise estimate for every machine, scene or game.

The current task does not rerun every intermediate implementation or sweep
every parameter combination. Mature saved forms represent the corresponding
experimental families. The following decisions have independent reasons and
are outside the clock-only reassessment:

- Incorrect intermediate variants remain rejected on correctness.
- Broad/dynamic Thumb continuations that add path accounting at runtime violate
  the requested static accounting constraint. The later static and transfer
  profitability gates are included.
- Whole-register division memoization was screened by full-input recurrence
  (0.0523%), independently of elapsed-time noise. Division-digit lowering is
  included.
- Executable-byte scanners, mutation versions and write-protection policies
  are superseded by the explicitly selected unsafe executable-byte default.
  Mapping identity and lifetime checks remain mandatory.
- Copying identity memory imposed a measured bulk-copy workload; it has been
  superseded by canonical direct backing. The current direct-span and lowering
  experiments are included; the discarded copied backing is not reconstructed.
- Compiler construction-only changes are outside this runtime question. The
  final state-pruning comparison includes its optimized compiler implementation.

## Host and provenance

Campaign root: `/home/claude/.scratch/eka-controlled/`. All plans were written
before their respective phase runs. Each phase has its own host state and
restores CPU policies and effective CPU masks before the next phase starts.
The target is 3.6 GHz, with actual counter-derived frequency approximately
3.592 GHz. The invariant reference counter was calibrated at 2304 MHz against
`CLOCK_MONOTONIC_RAW`; `tsc.c` and its binary remain in the campaign root.

The initial frequency-only pilot exposed additional variation while an unrelated
process shared the SMT sibling. Its observations are setup evidence, not final
performance verdicts. The decision runs reserve logical CPUs 7 and 15 and pin
the guest worker to CPU 7. Other user-space tasks retain the other 14 CPUs.

Two slow compact-dispatch Snakes controls and one Sky Force candidate failed
predeclared host-validity checks; they remain in the data with reasons. All
slow observations that pass those checks remain in the means. Further invalid
observations, if any, are likewise recorded rather than silently removed.

The primary controller loaded before later historical-harness/reporting edits.
Its exact source is saved as `isolated-controller.py`, verified against its
initial recorded SHA-256:
`d71a4553cff546407e3aa032ff889d584c1999e8f7bd57fd0ea7c337e5f90f30`.
Later per-command on-disk controller hashes are not substituted for that loaded
source. Subsequent phases record the controller hash at process startup.

The scheduler initially forced shared audio on. Before historical silent-audio
comparisons began, it was changed to preserve an explicitly selected setting.
This leaves the primary phase behavior unchanged: all its plans select shared
audio. The exact scheduler source hash is recorded for each subprocess.

Original replay/fault acceptance is linked through each historical report.
These timing runs compare guest instruction endpoints and presentation counts.
Capture-enabled runs additionally require identical presentation journals;
older capture-mode-2 runs may have no such journal. They do not constitute a
fresh full image/PCM correctness campaign.

The restoration helper records frequency governor/limits/EPP and cgroup masks.
During setup Linux 6.12 refused to restore empty inherited masks on three
populated cgroups. Their original effective availability was restored as an
explicit `0-15` mask. This is not byte-for-byte restoration of those fields;
no persistent service configuration was changed. Final restoration must be
checked after the final phase, rather than inferred from process completion.

After three clock-invalid Sky Force controls during unrelated Android build activity,
the first invocation stopped and restored the host. `isolated-host-resume1.json`
records the explicit resume. The resumed driver waits for known compiler/build
processes before each trial; this start rule does not retroactively discard
valid observations. The same frozen plan and measurement-validity limits apply.
