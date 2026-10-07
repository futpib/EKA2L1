# Recovered optimization graduation

The historical sweep is **paused at 64/109 game comparisons**: 515/872 valid
observations, 20 retained invalid attempts, and 45 comparisons remaining. Its
[checkpoint](CONTROLLED_SWEEP_CHECKPOINT.json) preserves the original plans and
partial observations. It has not resumed. The work below completes graduation
and the combined-default comparison on the current implementation.

## Adopted combination

Compiled syscalls use the zero-return trap protocol; sparse ROM lookup is enabled;
whole-entry budgets use a private precise fallback (mode 2). Full state pruning
and IR policy 17 remain selected. Division lowering, entry-only pruning, inline
budget recovery and static count batching remain opt-in. Direct memory remains
mode 2 and executable-byte policy remains 3.

## Combined bottom line

These are fresh comparisons of the exact final artifact against two untouched
archives. They include implementation and code-layout costs. Individual feature
percentages and historical gains are not added together. Positive CPU/wall values
mean greater throughput; negative native-instruction values mean less work.

| # | Baseline | Game | CPU throughput | Wall throughput | Native instructions | Faster CPU pairs | Candidate unpaced speed |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Repository 4a339da9a | Snakes | +1.70% | +1.07% | -2.13% | 4/4 | 1.940× |
| 2 | Repository 4a339da9a | Sky Force | +3.46% | +3.15% | -3.46% | 4/4 | 0.661× |
| 3 | Previously served LAN | Snakes | +9.45% | +7.64% | -10.40% | 4/4 | 1.950× |
| 4 | Previously served LAN | Sky Force | +9.62% | +8.85% | -7.08% | 4/4 | 0.660× |

Unpaced speed is guest time divided by wall time in the measured warm window,
at a fixed 3.6 GHz request; it is not boot/replay elapsed time or paced LAN speed.
Snakes uses guest seconds 78–96 and Sky Force 42–60. Every comparison uses four
fresh observations per artifact in ABBA then BAAB order. Paired agreement alone
does not prove that a small gain generalizes beyond these routes.

## Individual selection evidence

These off/on comparisons explain the choices; the combined table above is the
adoption result. ROM and budget experiments used the original restored syscall
protocol; revised syscall and division comparisons fixed ROM 1 and budget 2.

| # | Candidate | Snakes CPU | Sky Force CPU | Faster pairs, Snakes / Sky Force | Decision |
| ---: | --- | ---: | ---: | --- | --- |
| 1 | Revised compiled syscalls | +0.40% | +3.54% | 4/4 / 4/4 | Enabled |
| 2 | Sparse ROM lookup | -0.27% | +1.81% | 1/4 / 4/4 | Enabled; accepted game tradeoff |
| 3 | Outlined entry budget | +1.23% | +1.19% | 4/4 / 2/4 | Mode 2 enabled |
| 4 | Entry-only state pruning | -0.42% | +1.26% | 1/4 / 3/4 | Off; retain full pruning |
| 5 | Static count batching | -0.34% | +0.50% | 1/4 / 2/4 | Off; retain IR17 |
| 6 | Inline entry recovery | +0.31% | +1.01% | 2/4 / 3/4 | Off; use outlined recovery |
| 7 | Division digit lowering | +0.07% | -0.66% | 2/4 / 2/4 | Off |

The original syscall candidate produced +7.17% Sky Force in its same-binary
off/on comparison, but only +0.71% with mixed pairs against the untouched
repository baseline. That did not establish an overall win. The revised protocol
uses the existing zero chain-stop result for traps, so ordinary successful
regions avoid the added pending-trap load/test. Its exact comparisons and final
artifact measurements are recorded separately.

Division removes 0.38% of Snakes native instructions but produces effectively
flat CPU throughput and a negative Sky Force point estimate. Static batching
slightly increases native instruction counts. Neither earns a default change.
The outlined budget result is consistent in Snakes; its individual Sky Force
pairs are mixed. Sparse ROM trades a small Snakes cost for a consistent Sky Force
gain, as allowed by the requested game tradeoff.

## Controls and retained evidence

All 22 current-runtime game comparisons are complete: 176 valid observations
and 3 retained invalid attempts. Invalid runs were retried under the
original thresholds; no failed observation was deleted or accepted by relaxing
frequency criteria. The historical campaign remains separate.

CPU 7 and SMT sibling 15 were reserved, with support tasks kept on the other
cores. CPU min/max were requested at 3.6 GHz with performance governor/profile
and temporary charge inhibition. Reference-cycle frequency checks, thermal
checks, hardware-counter running fractions, worker identity, guest work, input
and presentation journals were validated. Mean and interval tolerances remain
0.5% and 1%; sibling activity remains bounded at 2%. Timing uses normal V8 and
hardware NVIDIA rendering without sampling, tracing, verifier or detailed
custom instrumentation. Shared host/GPU contention is not fully eliminated.

The original `4a339da9a` harness is shared by both baselines and the candidate.
Only post-measurement optional getter readbacks were added. Candidate baked
defaults are checked directly; unsupported legacy getters are reported as null.
The common harness does not configure the five new selectors. Build, harness,
controller and input hashes are recorded for every observation.

Artifacts, frozen plans, attempts and raw logs are retained under
`/home/claude/.scratch/eka-promote-recovered/`. The
[measurement snapshot](RECOVERED_DEFAULTS_RESULTS.json) preserves all phases;
[the experiment index](EXPERIMENT_INDEX.md) records current dispositions and
the separate historical results.

## Correctness and deployment

The [validation record](RECOVERED_DEFAULTS_VALIDATION.json) identifies the exact
final artifact and source hashes. The final compiler suite reports 172 passes
and zero failures; two diagnostic-only fixtures explicitly skip. The final
outlined-budget protocol passes 41,472 native/WASM syscall comparisons with
verification off/on and executable-byte modes 0/3. The prior revised-candidate
matrix covers IR17/18 and budget modes 0/1/2 with 145,152 comparisons. Division
passes 32,256 state/budget comparisons but remains disabled for performance.

Both final 60-frame game replays match native reference images, frame records
and PCM exactly. These correctness replays use software rendering. Actual
browser API tests verify baked defaults, configuration/readback, invalid values
and rejection of changes after initialization.

After timing, 88 live sysfs checks confirm restoration of CPU policies/EPP,
explicit/effective CPU masks, platform profile and charging.

The frozen artifact is served by the LAN launcher. Its running service path,
fetched WASM hash and browser-observed defaults match the measured artifact.
Both games, switching, keyboard/touch, narrow layout and manual loading were
exercised with hardware NVIDIA rendering. Saved screenshots were inspected to
confirm gameplay. `final-game-picker/` retains the browser/GPU report and images.
Paced playability is separate from the controlled unpaced throughput table.

**Live browser audio is not verified.** The integration check retains a failing
non-silent-audio assertion for each game. The host initially had both sound-card
profiles off; enabling the real built-in output temporarily did not resolve the
Chrome audio-device error. The original profile and default devices were restored.
An independent Chrome oscillator test, without the emulator, also reports a
running AudioContext whose clock stays at zero. The unchanged browser audio files
and exact native-reference PCM replays pass their checks, but do not substitute
for working live output. The complete picker check remains failed for this host
audio limitation. No benchmark or emulator default was changed to hide it.

## Paused continuation

The remaining 45 historical comparisons need an explicit continuation request.
Preserve existing valid observations and original plans, use fresh host-state
files, and resume the interrupted `cached-address-displacement/standard` panel
before proceeding through the remaining extended, inlining and memory entries.
No automatic resumer is running.
