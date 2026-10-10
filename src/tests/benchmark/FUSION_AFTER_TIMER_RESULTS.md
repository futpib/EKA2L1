# Fusion after fixing User::After timing

The timer correction is baked into both sides of every comparison, at commit
`7f1671110`. The ordinary production fusion policies remain enabled. This report
separates the newest complete Thumb helper-return extension from the broader
combination of retained guest-code fusion work.

The broad fusion comparison shows a large Snakes benefit and a smaller Sky
Force benefit in these gameplay windows. The newest complete Thumb call/return
extension alone still does not establish a Sky Force gain. Its small Snakes
increase needs caution because scene position and frame counts vary.

## Whole-game results

The timer fix is on in every cell. Changes are fused relative to that panel's
unfused control; CPU time reductions are negative. Raw measurements and paired
comparisons are retained in [FUSION_AFTER_TIMER_RESULTS.json](FUSION_AFTER_TIMER_RESULTS.json).

| # | Panel / game | Unpaced presentations/s A → B | Throughput change | Worker CPU ms/presentation A → B | CPU time change | Native instructions/presentation change |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Broad fusion / Sky Force | 101.04 → 102.73 | +1.67% | 5.483 → 5.251 | -4.24% | -2.27% |
| 2 | Broad fusion / Snakes | 28.89 → 44.14 | +52.80% | 29.954 → 17.897 | -40.25% | -35.62% |
| 3 | Recent Thumb extension / Sky Force | 102.87 → 101.87 | -0.97% | 5.316 → 5.302 | -0.27% | +0.06% |
| 4 | Recent Thumb extension / Snakes | 43.59 → 44.25 | +1.52% | 18.149 → 17.897 | -1.39% | -0.25% |

Both broad Snakes pairs improve wall frame throughput (+52.67%, +52.93%) and
CPU frame throughput (+68.11%, +66.63%). Both broad Sky Force pairs improve
CPU frame throughput (+5.14%, +3.72%), but their wall gains vary (+3.17%, +0.18%).
The broad bundle has a repeatable CPU benefit here; Sky Force's small wall gain
is less decisive than Snakes'. These percentages do not isolate each older
fusion feature's contribution.

For the recent extension, Sky Force's CPU pair directions disagree (-0.23%,
+0.76%), with essentially unchanged native instructions per presentation.
Snakes' two pairs improve CPU frame throughput (+2.15%, +0.68%), but its native
instruction reduction is only 0.25%. These short observations do not establish
a large gain from that extension after correcting the timer.

The broad Snakes result also exposes the virtual-time measurement problem:
unfused runs submit 179 frames in 6.19631 wall seconds; fused runs submit 307
in 6.95495 seconds. Guest-clock speed alone falls 3.873× → 3.451× (-10.91%),
even though displayed-frame throughput rises 52.80%. Completing the same guest
seconds sooner is not necessarily doing game work faster.

## Exactly what was compared

| # | Panel | A: unfused control | B: fused |
|---:|---|---|---|
| 1 | Recent extension | `thumb_complete_calls = false`; all earlier fusion retained | Current default, including complete Thumb calls |
| 2 | Broad fusion | ARM leaf resolver and Thumb immutable-code lookahead disabled for bounded translations | Current default, all retained fusion enabled |

The broad control removes ARM direct leaf/prefix/veneer and guarded indirect
call fusion, Thumb external-branch/callee-entry/complete-return fusion, and
Thumb folding of ARM import/syscall veneers. A call into such a veneer therefore
uses ordinary dispatch again. Both sides keep normal compiled regions, their
internal branches/loops, direct guest memory, semantic lowering, standalone
compiled syscalls, region-local state caching and the same 2 ms watchdog.
It does not disable the compiler or every optimization within a region.

The narrow control changes one initializer. The broad control adds two lines:
`if (bounded) leaves = nullptr` at the public ARM translator entry and
`if (bounded) immutable_code = nullptr` at the public Thumb entry. The exact
patches, build hashes and commands are archived in the JSON report. Neither
control patch remains in production source or the main build directory.

## Method and interpretation

Each panel uses A/B/B/A for each game: sixteen observations in total. Sky Force
covers guest seconds 58–70 and Snakes 74–86 with the same input and stock assets.
CPU 7 runs the emulation worker; sibling 15 is reserved. Frequency is requested
at 2.4 GHz and checked through hardware counters. There is no temperature gate.
Normal V8 tiering and hardware graphics are used; instruction diagnostics and
sampling are off. Both variants use a 10 ms capture-completion polling interval.

Throughput is pooled presentations / pooled wall seconds. Worker CPU cost and
native instructions are divided by pooled presentations too. A presentation is
a display submission, not an independently verified unique frame or simulation
update. Guest-clock progress is retained separately and is not used as the
useful-work speedup. The windows contain similar scenes but not identical work.
These short panels do not provide confidence intervals or cover every level.

The focused compiler check passes 4,800 chain/state/memory/callback comparisons
and 600 independent interpreter comparisons across the recent extension's
on/off cases. Production source and the ordinary build were restored before
each timing panel. The restored fused WASM is byte-identical to the tested and
served timer-fixed artifact. No new runtime/default/LAN change follows from
this measurement; the timer fix remains unconditional and fusion stays enabled.

## Scene review and raw observations

All sixteen start/end screenshot pairs were inspected. Every window contains
active gameplay, with no menus, loading screens or death screens. Sky Force
retains three lives. In the broad control, it starts with tutorial text over
combat and ends at score 675 / stage 3%; fused runs start a little later in the
level and end at score 775 / stage 4%. Snakes remains at score 200 in the same
red-snake arena but at different positions. These offsets constrain how
precisely the small differences can be attributed to compiler changes.

| # | Panel | Game | Variant/order | Presentations | Wall seconds | Worker CPU seconds | Native instructions, billions |
|---:|---|---|---|---:|---:|---:|---:|
| 1 | Recent | sky-force | A0 | 380 | 3.68503 | 2.010391 | 13.670144 |
| 2 | Recent | sky-force | B1 | 379 | 3.73290 | 2.009685 | 13.690661 |
| 3 | Recent | sky-force | B2 | 379 | 3.70829 | 2.009160 | 13.657075 |
| 4 | Recent | sky-force | A3 | 379 | 3.69333 | 2.024507 | 13.697273 |
| 5 | Recent | snakes | A0 | 151 | 3.46678 | 2.745259 | 14.973916 |
| 6 | Recent | snakes | B1 | 156 | 3.50860 | 2.776521 | 15.420513 |
| 7 | Recent | snakes | B2 | 155 | 3.51956 | 2.789316 | 15.395389 |
| 8 | Recent | snakes | A3 | 153 | 3.50734 | 2.771966 | 15.223777 |
| 9 | Broad | sky-force | A0 | 379 | 3.80285 | 2.082330 | 13.938964 |
| 10 | Broad | sky-force | B1 | 381 | 3.70547 | 1.991028 | 13.626035 |
| 11 | Broad | sky-force | B2 | 380 | 3.70239 | 2.004819 | 13.671669 |
| 12 | Broad | sky-force | A3 | 381 | 3.71893 | 2.084926 | 13.956684 |
| 13 | Broad | snakes | A0 | 90 | 3.11589 | 2.699567 | 13.911647 |
| 14 | Broad | snakes | B1 | 154 | 3.49226 | 2.747740 | 15.304547 |
| 15 | Broad | snakes | B2 | 153 | 3.46269 | 2.746630 | 15.234493 |
| 16 | Broad | snakes | A3 | 89 | 3.08042 | 2.662263 | 13.748087 |

All sixteen observations pass the predeclared frequency/isolation rules. Actual
mean frequency is approximately 2394.3–2394.4 MHz. Both panels restored host
frequency, affinity, cgroup and platform settings without errors. Logs were
checked between observations; there are no discarded or failed timing runs.

Raw logs, frozen artifacts, input files, control patches and reproducible
commands are under `/home/claude/.scratch/eka-fusion-after-20261010/`; the broad
panel is in its `broad/` subdirectory. The JSON report includes the patch text,
artifact/input/report hashes, all observations and all screenshot hashes.

WASM SHA-256:

- Current fused default: `83d6da16a57678aa143d42154d440710b172e076fca065afd12e7c461104497f`
- Recent extension off: `36fb0664697329e65200581c9205d5950c877295819332a8f9dbb2864c7a3d6c`
- Broad fusion off: `678f6c4f667cdfd03117f50ad198768bcc04d5d3aaddc3b8dad6a9aa250d4983`

The normal build and `.lan` continue to use the fused default with the timer
fix. The compiler settings, game/ROM blobs and production source are unchanged
by this follow-up. Only this report and the experiment catalogue are updated.
