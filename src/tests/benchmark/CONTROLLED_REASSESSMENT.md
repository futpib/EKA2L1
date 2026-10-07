# Reassessment of optimization timing decisions

The previous uncontrolled-clock measurements do not establish that small gains
or regressions came from an optimization. This campaign reopens those verdicts
using frozen builds, identical guest work, measured CPU frequency and a reserved
physical core. It includes recent changes and archived candidates that had
passed correctness but were rejected or left unresolved on noisy timing evidence.

[Live result table](CONTROLLED_RESULTS.md) and [measurement method](CONTROLLED_BENCHMARKS.md).

## Scope

The five fixed plans contain 70 candidate/control comparisons, comprising 109
game comparisons and 872 valid observations. Each game comparison uses ABBA
then BAAB. Historical Snakes-only experiments remain explicitly Snakes-only;
their results are not presented as Sky Force measurements. No production
optimization or default is changed by this task.

| # | Phase | Comparison | Original report |
| ---: | --- | --- | --- |
| 1 | reserved | compact-dispatch | [DISPATCH_AND_DIVISION_RESULTS](DISPATCH_AND_DIVISION_RESULTS.md) |
| 2 | reserved | division-digits | [DISPATCH_AND_DIVISION_RESULTS](DISPATCH_AND_DIVISION_RESULTS.md) |
| 3 | reserved | state-pruning | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 4 | reserved | trusted-lookup-inline | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 5 | reserved | entry-only-pruning | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 6 | reserved | store-only-pruning | [RUNTIME_STATE_RESULTS](RUNTIME_STATE_RESULTS.md) |
| 7 | reserved | direct-span-two | [DIRECT_SPAN_RESULTS](DIRECT_SPAN_RESULTS.md) |
| 8 | reserved | direct-span-three | [DIRECT_SPAN_THREE_RESULTS](DIRECT_SPAN_THREE_RESULTS.md) |
| 9 | reserved | thumb-static | [THUMB_STATIC_REGION_RESULTS](THUMB_STATIC_REGION_RESULTS.md) |
| 10 | reserved | thumb-transfer-gate | [THUMB_TRANSFER_GATE_RESULTS](THUMB_TRANSFER_GATE_RESULTS.md) |
| 11 | reserved | batched-counts | [BATCHED_INSTRUCTION_COUNTS](BATCHED_INSTRUCTION_COUNTS.md) |
| 12 | reserved | span-page-reuse | [SPAN_PAGE_REUSE_RESULTS](SPAN_PAGE_REUSE_RESULTS.md) |
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
| 66 | extended | compact-generated-memory | [MEMORY_AND_CONNECTED_RESULTS](MEMORY_AND_CONNECTED_RESULTS.md) |
| 67 | extended | connected-callee-loops | [MEMORY_AND_CONNECTED_RESULTS](MEMORY_AND_CONNECTED_RESULTS.md) |
| 68 | extended | guarded-successor-lookup | [MEMORY_AND_CONNECTED_RESULTS](MEMORY_AND_CONNECTED_RESULTS.md) |
| 69 | memory | tlb-unaligned-scalar | [UNALIGNED_SCALAR_RESULTS](UNALIGNED_SCALAR_RESULTS.md) |
| 70 | extended | compiled-syscalls-long-snakes | [COMPILED_SVC_TIMING_RESULTS](COMPILED_SVC_TIMING_RESULTS.md) |

Entries 66-69 correct omissions found while the first phase was running.
They were appended only to unstarted phases; earlier plan copies remain in the
campaign directory. Their archived WASM and loader hashes match the original
reports. The three older connected/memory trials retain their original silent
Snakes configuration. The TLB alignment trial covers both games. The running
phase, its orders and its validity thresholds were not changed.

The syscall report's original rejection also cited the long-snake route, which
uses `snakes-length-long.input` at guest seconds 42-60. The standard route does
not cover that input. Entry 70 was added as the first comparison of the unstarted
extended phase, preserving the prior plan as `extended-plan.before-long-syscalls.json`.
It uses the already-audited syscall binary and configuration. The input hash
matches all four original long-route observations; the plan also fixes their
guest instruction endpoints and presentation count. The running architecture
plan and its observations are unchanged. All eight live observations of this
additional route have now passed, including the original input hash, guest
instruction endpoints and presentation count. The syscall report records the
completed long-route results separately from the standard route.

The initial architecture controller predates optional input-route selection.
Its exact loaded source is preserved as
`architecture-before-long-input-controlled_comparison.py`, matching startup
hash `e08347286d13176478156774d18a57365983e79239ce48de434d778c7d0b0915`.
Per-command hashes of later on-disk files do not replace that loaded-source hash.

After 128 valid architecture observations, the first `mixed-ir` launch failed
before browser startup: the archived interactive server parser rejected the
profiling harness's `-1` sentinel for leaving unavailable options untouched.
The helper restored all 16 CPU policies, EPP, the balanced platform profile and
full effective CPU masks; these were also checked live before resuming.
`architecture-host.json` records the failure and successful restoration.

Only the scratch copy of `harness-164173789` was adjusted: its `profile.ts`
now calls `startServer(0, files, undefined, {compilerPolicy: {}})` so the unused
interactive launcher does not parse the profiler's options. The profiling
harness still applies and checks the selected emulator options itself. No
frozen binary, plan, measurement window or validity threshold changed. The
original file, exact edit and before/after hashes are preserved in
`legacy-harness-policy-fix.json` and its referenced backup. The original
failure log is retained with an `.interrupted-...` suffix. A startup check
reproduced the rejection and verified the corrected HTTP path; the resumed
real browser control then passed with compiler policy 7 and valid counters.
`architecture-host-resume1.json` records the resumed invocation, which loads
the newer controller with optional input-route support. Completed observations
are retained, and later phases remain serial.

The extended phase stopped three times after three clock-invalid attempts at
`guard-publication-omission/combat`, reversed-order first control (nine attempts
in total). Whole-window averages were close to the requested frequency, but
individual intervals fell below the predeclared 1% limit. Other validity checks
passed. All nine attempts remain in the data; the much slower earlier candidate
that passed the checks remains in the performance comparison. All three invocations
restored the original CPU policies, EPP, balanced profile and effective CPU
masks, also checked live (`extended-host.json`, `extended-host-resume1.json`,
`extended-host-resume2.json`).

A separate read-only MSR diagnostic caught actual ratio-8 throttling while the
HWP minimum and maximum requests remained 36. Core and package status bit 2
were asserted while the core's internal thermal-status bit 0 was clear. Intel
identifies bit 2 as another platform agent asserting PROCHOT/FORCEPR; the
asserting component is not identified by these measurements. See the
[Intel thermal-management reference](https://cdrdv2-public.intel.com/835755/253669-sdm-vol-3b.pdf).
The user reported a power outage; AC was online during the diagnostics. That
provides relevant power context, but does not establish which component caused
the pulses.

Four diagnostic replays are excluded from performance results because they
also sampled MSRs. The original settings and a trial lowering support-core
requests to 2.3 GHz failed the clock rule. Two trials temporarily inhibiting
battery charging passed their measured windows at approximately 3591.6 MHz,
although each still recorded three external-throttle samples during startup.
This does not establish charging as the cause. All temporary CPU/profile/mask
changes and charging inhibition were restored and checked; each diagnostic restored charging
to `auto`. Raw samples, validation and restoration records are collected in
`external-throttle-diagnostics.json` in the campaign directory.

The third original-settings invocation again failed all three attempts.
Before the next launch, `charging-stability-boundary.json` recorded a temporary
charging pause for the remaining extended, inlining and memory observations.
The wrapper restores the previous charging mode on every phase exit, including
failure, and records both transitions. `extended-host-resume3.json` and
`extended-charging-resume3.json` describe the first such invocation. The same
plans, run order, 3.6 GHz request, affinity and clock rules remain in force. The first resumed control passed at
3591.605 MHz, with every measured interval inside the unchanged limits.

The completed guard-publication Sky Force comparison spans this host-setting
boundary: five valid observations preceded it. They remain included, including
the slow candidate; do not interpret that mixed-condition comparison as a
clean estimate of a small gain. Subsequent comparisons start with charging
paused for both variants. The report requires charging restoration as well as
CPU/profile/mask restoration before declaring completion. The Chromium version
remains 153.0.8010.52.

During `wide-result-reuse`, an unrelated `cargo test -p slopd-acp` process
started at 10:29:38 UTC inside the first panel's second candidate window
(approximately 10:29:36-10:29:52 UTC). That candidate passed every predeclared
host check and remains included. The process lifetime establishes overlap,
not how much interference it caused; the parent's CPU use does not quantify
its children. The timestamp evidence is preserved in
`wide-reuse-concurrent-test-observation.json`. Subsequent control attempts
failed the unchanged clock rule while additional build activity was present.
The harness records these as invalid and waits before the next launch; it
never stops unrelated jobs. This comparison therefore needs its shared-host
limitation considered alongside its paired results.

Three control attempts then failed the clock rule; the last also recorded a
package-throttle counter increase. The extended phase stopped and restored
CPU policies, EPP, the balanced profile, effective CPU masks and automatic
charging; all were checked live. At that check, CPU sensors read 49-57 C and
no known build process remained. `extended-host-resume3.json` and
`extended-charging-resume3.json` preserve the restoration. A separate resume
preflight waits for 60 continuous seconds without known build processes and
with CPU sensors at or below 70 C, recorded in `post-build-settling-resume5.json`.
The check passed after 60.27 seconds, ending at 48 C.
It then resumes the same plan and unfinished control, using the existing
3.6 GHz/core reservation/charging-pause setup and unchanged validity rules.
New host and charging records use the `resume4` suffix; the queue/log invocation
is named `resume5`. Earlier valid observations are not rerun or discarded.

Builds kept starting after short quiet gaps during `compare-operand-reuse`.
One control failed the clock rule, and repeated post-trial build waits showed
that the one-second launch check was admitting gaps inside an ongoing series
of jobs. After the reversed-order first candidate was saved, the controller
was stopped between trials. CPU policies, EPP, profile, effective CPU masks
and automatic charging were restored and checked live. The boundary record
is `build-settling-boundary-stop.json`; the exact earlier controller is saved
as `extended-before-build-settling-controlled_comparison.py`, SHA-256
`c167a24aeb165c904c06269ec89e30fff814c15284472710ca07d7080ce49543`.

Only the launch wait was strengthened: after seeing a build, the controller
requires 300 continuous quiet seconds, restarting the interval if another
build appears. Already quiet trials retain their one-second check. Eleven
harness tests pass, including quiet-host startup, the full five-minute interval,
and a new build interrupting the quiet gap. Existing observations, plans,
binaries, run order and measured validity thresholds are unchanged. The new
`resume6` queue first waits for 300 quiet seconds with CPU sensors at or below
70 C while normal host settings remain restored. Its preflight record is
`post-build-settling-resume6.json`; subsequent host/charging records use the
`resume5` suffix. This reduces launches between build waves; it cannot prevent
new unrelated work from starting after a trial has begun.

## What these measurements can establish

Recent frozen builds answer the marginal runtime question at the same source
stage where each decision was made. Archived compiler, dispatch and inlining
experiments answer the corresponding historical question. They do not prove
the same change wins on the current direct-memory/default compiler combination.
A recovered historical win is a reason to port and measure that change again,
not to add its percentage to current throughput.

Two limit screens (`conditional-leaf-bound` and `hot-source-window`) use the
later frozen tail-prefix archive, with prefix features disabled and conditional
leaf fusion enabled. They recheck the bounds in that configuration; they do not
reproduce the earlier pre-conditional compiler from the original limits report.
`current-tlb-vs-direct` intentionally uses the current retained production build.
An archive audit reconciled all 138 build selections against the experiment or
baseline archive reports. Its paths, hashes and these stage distinctions are
recorded in `provenance-audit.json` in the campaign directory.

An additional report/plan audit checked 7,310 reported selector values across
511 completed attempts, including retained clock-invalid attempts. It found no
mismatches and no missing selector that distinguishes a candidate from its
control. `observed-selector-audit.json` and its reproducible script preserve
the audit. These are checks of reported settings; runtime getter assertions
depend on the archived harness. The older batching and span-page-reuse reports
lack `memory_impl` (32 observations in total): their fixed TLB implementation
predates that report field, so no dynamic memory-mode readback is claimed.

All 511 attempts at this audit cutoff report `Chrome/153.0.8010.52`, with no
missing browser version or mixed-version comparison. The recorded cutoff is
preserved in `browser-version-audit.json`; these audits must be refreshed after
the remaining observations.


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

## Corrected support-thread isolation

A live `/proc` check found the Python measurement process allowed on CPUs 0-15,
with last CPU 15. It and its clock thread were inside the benchmark cgroup, so
excluding other cgroups did not isolate the core from the measurement machinery.
The Chrome support threads themselves were correctly excluded. This gap could
interfere with the guest worker; it is not an established explanation for any
particular slow observation.

The old `isolated` cohort is preserved in full as
[preliminary results](CONTROLLED_PRELIMINARY_RESULTS.md), with 48 observations
valid under its earlier rules plus all invalid observations and interrupted
artifacts. No selected slow result is removed. All completed pairs are repeated
in a fresh `reserved` phase. Its plan requires controller and monitor affinity
to exclude the reserved CPUs. Later, not-yet-started phase plans require the
same check. `reserved-plan.json` and `reserved-runs/` identify the corrected
campaign, while old paths and hashes remain intact.

The frequency helper now gives itself and its children the support CPU mask.
The scheduler probe also pins itself before spawning its monitoring thread,
and records that monitor's actual affinity in every sample. A live failing-child
test confirmed the child inherited only support CPUs and host policy/masks were
restored afterward; the unit validator rejects missing or conflicting monitor
affinity. Three fresh Snakes browser runs also passed: every clock sample recorded worker
affinity `[7]` and monitor affinity `[0,1,2,3,4,5,6,8,9,10,11,12,13,14]`.

## Platform power profile

After the first five corrected game comparisons, repeated actual-clock failures
prompted a check of the laptop's platform policy. Although CPU limits requested
3.6 GHz, its balanced platform profile reported a 30 W sustained MMIO package
limit. The supported performance profile reported 35 W. A deliberate child-failure
test verified restoration of the original balanced profile, CPU policies and
effective CPU masks. The package limit returned to 30 W as well.

The first five complete comparisons preceded the platform-profile switch:
compact dispatch and division lowering in both games, and state pruning in
Snakes. Balanced was the existing host setting; the earlier sampler did not
record that setting in every sample. Actual frequency, CPU policy and isolation
were still checked under the original rules. The controller stopped only after
that eighth state-pruning observation; `reserved-host.json` confirms restoration.
The deliberate profile switch occurred between complete comparisons. The
remaining comparisons use performance,
with the same 3.6 GHz request, binaries, game windows, run orders and clock limits.
Every subsequent clock sample records the profile. The four unstarted plans
explicitly require performance; the resumed original plan retains its hash and
also rejects a profile change within a run.

`platform-profile-restore-test.json` records the failure-path check, and
`reserved-host-performance.json` identifies the resumed wrapper. Exact pre-change
controller and sampler sources are retained as `reserved-before-platform-*.py`.
The measured frequency rules remain authoritative; a larger platform power
allowance alone is not evidence that a trial ran at the requested frequency.
