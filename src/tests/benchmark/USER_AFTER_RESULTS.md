# User::After tick deadlines

**Adopted timing correctness fix.** Positive `User::After` delays now use the
nominal 64 Hz tick queue. This removes the incorrect one-microsecond wakeup for
`User::After(1)`, including the one in Sky Force's idle callback. `AfterHighRes`
uses the separate existing nanokernel timer model. No experiment switch is needed.

The whole-game ABBA screen improves Sky Force's unpaced presentation throughput
**10.95%** and reduces guest-worker CPU time per presentation **16.45%**. Snakes
is effectively flat: throughput **-0.65%**, worker CPU time per presentation
**+0.46%**. These are short, reviewed gameplay windows, not helper benchmarks or
an all-level performance guarantee. Raw observations, commands, hashes and
paired comparisons are in [USER_AFTER_RESULTS.json](USER_AFTER_RESULTS.json).

## Upstream and timing behavior

Fresh upstream `master` at `11f5b6cd141c0884a130ee45743f1f5e17bb1783` was merged
into `wasm-port` as `b29bf4c22fccd8ae82122d04deeae5a8416991fe` before this fix.
Its six new commits update translations and handle Qt command-line help before
GUI initialization. The merge preserves local device-install, dyncom and frame
dump options in the shared command registrar. Upstream did not fix this timer
path. The post-merge WASM is byte-identical to the preceding deployed build.

Symbian's [Exec::After implementation](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/kernel/eka/kernel/sexec.cpp)
sends positive delays through the thread's tick timer and handles nonpositive
intervals separately. The [tick queue implementation](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/kernel/eka/kernel/stimer.cpp)
rounds the requested deadline upward, including the phase within the current
tick. `AfterHighRes` has a distinct nanokernel path.

```text
before:
    User::After(us)        -> wake thread at now + us
    User::AfterHighRes(us) -> the same handler

after:
    User::After(us > 0) -> wake at ceil((now + us) / 15625) * 15625
    User::After(0)      -> deadline now, no tick-sized delay
    User::AfterHighRes  -> existing high-resolution nanokernel deadline

unpaced event loop, unchanged:
    advance toward earliest pending event, bounded by current time slice
    service due events and resume runnable threads
```

At guest time 10,000 us, `After(1)` now targets 15,625 us. An unrelated event
at 12,000 us still runs first. This is deadline rounding, not an unconditional
15,625 us clock jump. Relative internal/host-pacing sleeps retain their relative
delays. Absolute scheduler deadlines avoid adding syscall bookkeeping time to
a deadline computed on the native moving clock.

The nominal tick model does not reproduce physical nanokernel rounding jitter.
Zero retains its existing immediate scheduled-completion mechanism; this patch
does not newly implement the native kernel's exact ready-list rotation.

## Controlled whole-game results

A is the post-merge control; B adds this fix. Each game runs A/B/B/A, with the
same 2,000 us watchdog, replay input, game assets and default compiler/direct
memory configuration. Sky Force covers guest seconds 58–70; Snakes covers
74–86. Diagnostics and Chrome sampling are disabled for timing.

| # | Game | Unpaced presentations/s A → B | Throughput change | Worker CPU ms/presentation A → B | CPU time change | Native instructions/presentation change |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Sky Force | 92.35 → 102.45 | +10.95% | 6.332 → 5.290 | -16.45% | -14.11% |
| 2 | Snakes | 44.33 → 44.04 | -0.65% | 17.885 → 17.968 | +0.46% | -0.32% |

Ratios use pooled presentations divided by pooled wall/CPU time. Both Sky Force
pairs improve wall frame throughput (+12.96%, +9.00%) and CPU frame throughput
(+20.26%, +19.11%). Snakes CPU pair directions disagree (+0.36%, -1.27%); its
sub-percent pooled changes do not establish a meaningful gain or regression.
Renderer-process CPU time per presentation changes -3.29% and -0.43%, respectively;
this includes its other threads but excludes GPU device time.

| # | Game | Variant/order | Presentations | Wall seconds | Worker CPU seconds | Native instructions, billions |
|---:|---|---|---:|---:|---:|---:|
| 1 | sky-force | A0 | 377 | 4.09620 | 2.407632 | 15.921914 |
| 2 | sky-force | B1 | 380 | 3.65521 | 2.017919 | 13.740983 |
| 3 | sky-force | B2 | 381 | 3.77256 | 2.008095 | 13.712484 |
| 4 | sky-force | A3 | 377 | 4.06882 | 2.366811 | 15.747862 |
| 5 | snakes | A0 | 154 | 3.47526 | 2.758869 | 15.279683 |
| 6 | snakes | B1 | 154 | 3.48920 | 2.749005 | 15.236570 |
| 7 | snakes | B2 | 152 | 3.45839 | 2.749192 | 15.047798 |
| 8 | snakes | A3 | 155 | 3.49518 | 2.767719 | 15.400812 |

All eight start/end screenshot pairs were reviewed: Sky Force remains in active
combat, stage 0% → 4%; Snakes remains in the active red-snake arena, score 200.
Neither game is measured in a menu or death screen. Sky Force's total submissions
are 754/761 and Snakes' are 309/306: these are similar, not identical workloads.
Submissions are not independently counted game updates or unique frames.

Guest-clock progress alone would report 2.939× → 3.231× for Sky Force and
3.443× → 3.454× for Snakes. Those ratios are recorded separately and are not used
as the useful-work speedup. Correcting a clock contract can change the number
of updates performed in a fixed virtual-time interval.

CPU 7 ran the guest worker; SMT sibling 15 was reserved and browser helpers were
kept elsewhere. Requested frequency was 2.4 GHz, with actual run means around
2394.3 MHz. Every run passed the fixed-frequency and sibling-busy checks.
There was no temperature acceptance gate. Hardware counters measure the worker,
with the capture-completion polling interval shortened to 10 ms in both arms
of the private harness to reduce boundary overshoot. Host frequency, affinity,
cgroup and platform settings restored successfully, with no restoration errors.

## Validation and artifacts

- Native Qt/test builds pass. Native CTest passes 3/3 after both the merge and fix.
- Qt `--help` succeeds without DISPLAY/WAYLAND_DISPLAY and retains local options.
- WASM runtime builds pass. All 183 compiler tests pass after the merge; the timer
  patch does not change the compiler.
- New tests cover positive sub-tick delays, tick boundaries, longer intervals,
  zero, high-resolution separation, and earlier unrelated events under bounded
  virtual-clock advancement.
- Served, hardware-accelerated paced gameplay passes for both games with input:
  Sky Force 32.00 presentations/s, Snakes 15.99, both 1.00× virtual/host time over
  20 seconds. Those checks are muted functional checks, not performance A/B runs.

The broader `game-picker.ts` check passes switching, keyboard/touch, layout and
manual launcher checks, but **fails both non-silent audio checks**. Chromium
reports an audio-device/WebAudio-renderer error and its AudioContext time stays
at zero. A standalone oscillator with no emulator reproduces the zero-clock
behavior. Guest PCM is produced, but audible output is not verified on this
host. The complete failed report and independent host control are retained in
the results snapshot. Sky Force begins this broader flow in ship selection and
ends in combat; its timing is not used as a gameplay performance window.

Raw logs and frozen baseline/candidate assets:
`/home/claude/.scratch/eka-user-after-20261010/`. The first pilot failed before
emulator launch because auxiliary touch-control assets were missing from the
snapshot. Both snapshots were corrected before the successful pilots and all
reported comparisons. That failed log remains retained; pilots ran uncontrolled
and supply no performance evidence.

WASM SHA-256:

- Control: `8cea053b9b7dfe2b299a3b069172928fab7819d2a0267e9ed403283bca6d487a`
- Fixed: `83d6da16a57678aa143d42154d440710b172e076fca065afd12e7c461104497f`

No game, ROM, input replay, compiler policy, watchdog interval or global virtual
clock advancement rule was modified for this comparison.

The fixed artifact is deployed at `https://claude-laptop.lan:8188/`. All seven
served WASM/data/script/style assets match the tested frozen candidate, and the
served HTML names its WASM hash. TLS was checked against the local LAN CA. The
private test server was stopped after validation; the LAN service remains active.
