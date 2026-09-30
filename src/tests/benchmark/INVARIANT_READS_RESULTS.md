# Invariant read spans: modest measured gain, delivered on LAN

The exact tested archive is served at https://claude-laptop.lan:8188/ with
explicit policy 4 and eager regions off. Reload to use it. Policy 4 proves
selected read spans through registers that cannot change in a region, including
its loops and inlined leaves. It uses original emission; general integer IR
policies remain unselected. Actual loads still execute in order. Budgets,
interrupts, callbacks and code-write guards retain precise behavior. Ordinary
launcher defaults remain unchanged; this deployment explicitly selects the
verified policy. See INVARIANT_READS_DESIGN.md.

## Performance

Shared audio, physical GPU, guest seconds 78–96; 3,975,618,624 instructions and
676 presentations per run. Each warmup and run was serial, with sampling and
detailed counters off and no owned heavy jobs overlapping.

| Batch | Original, same binary | Read proofs | Previous served | Throughput vs original / previous served |
| --- | ---: | ---: | ---: | ---: |
| A | 13.44370s | 13.10500s | 14.01840s | +2.58% / +6.97% |
| B | 13.53210s | 13.59230s | 13.83110s | -0.44% / +1.76% |
| C | 13.60145s | 13.14590s | 14.02825s | +3.47% / +6.71% |

A order: proof/plain/served/served/plain/proof.
B order: plain/proof/served/served/proof/plain.
C order: served/plain/proof/proof/plain/served.
The candidate occupies outer, intermediate and inner paired positions. This is
partial position counterbalancing of all variants, not a complete Latin-square
or randomization scheme. Application bytes and compiler/runtime sources remain
unchanged across these timings; only the fault probe was extended between A/B.

Across all 18 runs, original mean is 13.52575s, proof 13.28107s and previous
served 13.95925s: 1.84% higher throughput against matching original emission,
5.11% against previous served. Two of three batch means and five of six adjacent
pairs favor proof over matching original. All three means favor proof over
previous served. Every observation remains, including the 14.025s proof trial
and slow served controls. Ranges overlap. These are modest repeated host-specific
results, not a statistical-significance claim or a fixed improvement promise.
The previous-served comparison includes other retained fixes/optimizations;
same-binary policy comparison is the more specific evidence for this change.

All six proof trials execute 18 guest seconds in 13.0209–14.0250 host seconds,
1.28–1.38 times realtime in this workload. Nine/eleven reads select the proof in
the captured busy loops. Hot bodies shrink from 19,852/12,083 to 18,780/10,862
bytes, but complete modules grow from 20,070/12,299 to 38,890/23,201 bytes because
they retain private original fallbacks. No memory-use or compilation-speed gain
is claimed. Policy 0's fixture modules retain their earlier original bytes.

## Verification

All 145 compiler tests pass, including 15,360 new exact progress/state/memory
comparisons with short budgets and pending interrupts. Both original and proof
policies pass checked replay against 1,600 native images, guest records and
4,919,249 stereo PCM frames. Native CTest passes all three targets; frontend
passes nine checks. The known native-identical movement-heuristic limitation
remains separate from exact equality, not a newly passing check.

The expanded production fault probe passes 7,648 freshly rebuilt native/WASM
comparisons per policy 0/4, 15,296 total. Its new 64-case remapping fixture uses
an unproved byte or multi-register load whose fault callback replaces the proved
root page. It independently asserts the first read uses the old mapping and
later reads use the new backing, in addition to comparing callback-visible state.
The compiler-test binary and application JS/WASM remain byte-identical after
this probe extension; prior compiler/replay passes are reused for those unchanged
bytes, not claimed as additional fresh runs. New probe archive is
invariant-reads-remap-candidate. All comparisons were recomputed from saved raw
logs with the final comparison tool; the 64-case mode omits the legacy unmapped
summary because its root-permission dimension is read-only versus read/write.

Two independent 120-second live/audio routes pass with applied policy recorded:
manual startup achieves 1.000100 times realtime and 15.40 ms maximum sampled
lag; automatic startup 1.000119 and 5.94 ms. Both have zero additional gameplay
audio underruns or dropped samples. Startup has 6/7 recovery underruns before
the measured window; startup audio is not fixed. Keyboard/touch, visible gameplay,
layout, mute/unmute and shutdown pass. Captures were visually reviewed. Chromium
uses physical NVIDIA ANGLE Vulkan rendering. Launcher controls validate explicit
policies before real initialization; default omission and invalid policy rejection
were also checked. Nine frontend checks pass freshly after the launcher changes.
The application was not rebuilt for these launcher-only changes.

The actual HTTPS launcher passes secure-context/isolation, applied policy,
gesture sound, measured mute/unmute, keyboard/touch, layout, visible display and
shutdown checks without page/request/HTTP errors. Downloaded JS and WASM hashes
match the timed archive. No push occurred.

## Artifacts and reproduction

Archive: /home/claude/.scratch/eka-benchmark/invariant-reads-candidate.
Source base 1e9c112b8db021b022f941f9b5699376142689c1 plus archived patch;
implementation 420e5df10, callback tests a0e143cda, live controls c54c81c9e.
Application WASM SHA256:
426038540c288ed57e721169080b643252629a4891596f0fbd70dc3b30479a52.
Loader SHA256:
d7361195b875116cb6e735d58a011ecb950efd19f631f201f5c5691598c1e3e4.
INVARIANT_READS_EVIDENCE.json retains raw timings, comparisons, provenance,
live reports and HTTPS delivery evidence. Large replay/probe logs are in the
corresponding /home/claude/.scratch/eka-benchmark/invariant-* paths.

Timing: EKA2L1_SHARED_AUDIO=1 with serial_variants.py ASSETS NEW_OUTPUT
proof=ARCHIVE plain=ARCHIVE served=OLD_SERVED --ir-mode proof=4 --ir-mode plain=0
--eager-regions proof=0 --eager-regions plain=0. Reorder variant arguments as above.
Live: EKA2L1_WASM_BUILD_DIR=ARCHIVE EKA2L1_AOT_IR_MODE=4 EKA2L1_AOT_EAGER_REGIONS=0
EKA2L1_LIVE_AUDIO=1 node live.ts ASSETS NEW_OUTPUT 120. Add
EKA2L1_LIVE_AUTOSTART=1 for automatic startup. Serve the archive with the same
compiler/eager environment. HTTPS smoke: invariant-reads-lan.mjs in scratch.
