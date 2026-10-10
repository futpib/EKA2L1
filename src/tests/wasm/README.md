# Browser game launcher

Build `eka2l1_wasm` following the [browser build instructions](../benchmark/README.md),
then run `npm ci --ignore-scripts` and `npm run serve -- 8188` in this directory.
Choose **Sky Force** or **Snakes** and press **Play**. Each launch starts a
fresh session; saves are not persisted. **Load my own files** opens the
ROM/RPKG/SIS upload controls. Changing the dropdown alone does not stop play.

Direct links use `?game=sky-force`, `?game=snakes` or `?game=custom`.
All browser execution uses the watchdog without instruction accounting.
Legacy `counting` URL parameters are ignored and removed on Play.
An optional
third command-line argument, such as `node serve.ts 8188 Snakes`, selects an
automatic default. Without it, the page waits for a choice.

Use arrows/WASD to move, Enter/Space to select or fire, and F1/F2 for softkeys.
During play, the FPS display below the menu button shows emulator presentations
per second over a rolling window of about two seconds. It updates twice per
second, includes stalls, and resets its sampling window after a background tab.
Open **☰** for game selection, sound, full screen and control settings.
Touch controls overlay the game without a permanent toolbar or control deck.
Touch uses independent movement/action areas,
with fixed or floating movement, saved per-game layouts, handedness and an
expandable phone keypad. See the [touch control guide](TOUCH_CONTROLS.md).
Sky Force first shows a splash screen and language selection, followed by
**Start game**, difficulty and ship selection.

The bundled choices share the stock Nokia 5320 ROM/RPKG. Games are installed and
launched by UID, avoiding ambiguous captions. Sky Force is the original game's
S60 3rd Edition release, version 1.22 at 240×320, UID `0xa020d913`.
No game or firmware patch is applied for Sky Force.

[`games.json`](games.json) records filenames, IPFS CIDs, SHA-256 digests and the
Sky Force archive source. Binaries stay outside Git, following the existing
[individual-file IPFS convention](../benchmark/README.md#assets).
The archive member is used unchanged; only its local filename is normalized to
`SkyForce.sis`. The downloaded SISX header identifies the same package UID.

The launcher checks cached and downloaded asset hashes before serving. Its asset
directory defaults to `/tmp/eka2l1-serve` (the system temporary directory on other
hosts); override with `EKA2L1_ASSET_DIR`. To retrieve Sky Force from a running IPFS
node:

```sh
ipfs cat bafybeie2g5e5ev5yaupq7czfeovdcg7oxygpjrxuncy2ypnxsnonliugbe > /tmp/eka2l1-serve/SkyForce.sis
```

The existing HTTPS host/certificate and compiler-policy environment variables
still apply; see [LAN setup](../benchmark/REALTIME_PLAYABILITY.md#lan-https-launcher).
The local launcher is at <https://claude-laptop.lan:8188/>.

`EKA2L1_WATCHDOG_US` controls the external yield-request interval, defaulting
to 2000 microseconds. Values 1..1000000 are accepted; zero cannot disable it.
A separate worker requests yields. Statically proved finite regions finish
without polling. Compiled chains have no instruction or region-count limit.
Paced play advances guest timers from elapsed host time at scheduler boundaries;
unpaced runs advance virtual CPU slices on yields and skip idle time. Explicit
pauses do not advance the clock. Native CPU backends retain their scheduling
budgets, and debugger single-step remains available.
The instruction-count verifier, generated count/budget checks and their
configuration APIs are removed. `EKA2L1_ENTRY_BUDGET` is retired. Historical
comparisons are in [watchdog results](../benchmark/WATCHDOG_RESULTS.md).
For output-based benchmark comparisons, `EKA2L1_BENCHMARK_START_FRAME=/path/to/frame.png`
makes `benchmark.ts` wait for that exact image before capturing its frame sequence.

Memory translation defaults to direct memory (mode 2) in WASM play. Set
`EKA2L1_MEMORY_IMPL=0` before starting the launcher to use the original-index
512-entry TLB. `EKA2L1_TLB_HASH` and `EKA2L1_MEMORY_CACHE` are retired
and must be removed from launch environments. See the
[cache comparison and adoption](../benchmark/MEMORY_CACHE_RESULTS.md).

Eligible ARM short blocks always use inline memory access. `EKA2L1_ARM_MEMORY`
is retired and must be removed from launch environments. The TLB/direct backend
selection below remains independent of this fixed compiler behavior. See the
[eligibility and adoption checks](../benchmark/ARM_MEMORY_ADOPTION.md).

For compiled-region replay and profiling, set `EKA2L1_BENCHMARK_AOT=5`.
Only 5 (full compiler) and 0 (interpreter reference) are supported; intermediate
stages 1–4 are retired.
These runs select direct memory and unsafe code mode 3.
Interpreter and mutation-compatible runs select TLB unless explicitly
overridden. The launcher also selects TLB for `EKA2L1_UNSAFE_CODE=0` when no
memory override is supplied. Only values 0 (TLB) and 2 (direct) are
supported; the separate direct-policy and delayed-activation controls are
removed. See [memory implementation selection](../benchmark/README.md#memory-implementations).
Direct scalar loads/stores permit unaligned addresses: the arena checks the
whole access range, and the fallback table requires the access to fit in one
page. TLB retains its alignment checks. See the
[alignment measurements and adoption](../benchmark/UNALIGNED_SCALAR_RESULTS.md).

Compiled syscalls, sparse ROM lookup, Thumb inline memory and trusted lookup
are fixed normal-launch defaults. Remove `EKA2L1_COMPILED_SVC`,
`EKA2L1_SPARSE_ROM_LOOKUP`, `EKA2L1_THUMB_MEMORY` and `EKA2L1_HOTPATH` from
launcher environments. Their pre-init APIs and the dedicated `benchmark.ts` /
`profile.ts` controls remain for targeted comparisons and fallback tests.
Trusted lookup still respects mapping lifetime and the executable-byte policy.
Full state pruning and IR mode 7 are the defaults. Former mode 17 performed the
same lowering after instruction accounting was removed and is now rejected.

The count-based exit classifier/recorder has been removed; compilation-site,
memory/fault metadata and invalidation diagnostics remain. Old count-returning
kernel harnesses live in [the historical archive](archive/counted-kernels/README.md).
See [cleanup scope and verification](CONFIGURATION_CLEANUP.md).

Division lowering, entry-only pruning, static count batching (IR mode 18),
and inline entry-budget recovery (mode 1) were removed after the completed
controlled sweep. Remove `EKA2L1_DIVISION_DIGITS`, `EKA2L1_ENTRY_ONLY_PRUNING`,
and `EKA2L1_EXECUTION_LIMITS` from launch environments, even if set to zero or
the former default. The limits are now fixed: 512 source bytes, 32 leaf
instructions and 8 inline sites. Their read-only API reports `512,32,8,0`,
where the final zero means no region-count cap. See the [56-entry cleanup audit](../benchmark/RETIRED_SWEEP_EXPERIMENTS.md).

Run the actual browser integration check against a running launcher:

```sh
node game-picker.ts https://claude-laptop.lan:8188/ /absolute/path/to/new-results
```

It launches both games through the picker, exercises keyboard/touch, measures
guest time and frame presentations, checks non-silent browser audio and narrow
layout, switches games and opens manual loading. It retains intermediate menus,
gameplay screenshots, GPU details, browser logs and measurements. Inspect those
screenshots to confirm the intended game scenes; changing pixels alone do not
prove gameplay. Audio failures are retained while the other game and launcher
checks finish, then cause the complete check to exit unsuccessfully. Device
clock and PCM-queue state are included to distinguish browser output failures
from missing emulator samples. Performance and audio underruns are reported,
not hidden behind a boot-success assertion.

To measure paced gameplay through the launcher:

```sh
node paced-gameplay.ts https://claude-laptop.lan:8188/ /absolute/path/to/new-results chrome snakes 30
```

The last arguments select `chrome` / `firefox`, `snakes` / `sky-force`, and the
measurement duration in host seconds. Run combinations
serially with fresh output directories. Firefox needs working WebGL (on this
Linux host, the existing `DISPLAY` and `XAUTHORITY` are supplied). The script
uses an isolated browser profile that accepts the local HTTPS certificate.
It exercises the game picker and Play button, checks that retired APIs and the
counting selector are absent, verifies watchdog requests and zero instruction totals, and samples presentations and guest time with sound muted.
It also records installed compiled-function counts and elapsed microseconds in
EKA2L1 translation, module emission and synchronous installation. Compilation
during the window remains included in FPS. The first presentation snapshot and
RAF-observed frame gaps separate startup from the measured window; a first
presentation need not contain a visible game image. Menu presses start after
that presentation and use relative guest-time waits, so a loading pause cannot
collapse several input deadlines together. Menu screenshots are retained too.
Screenshots are taken outside the measurement window; inspect them to confirm
gameplay. The result measures paced playability.

Loaded ARM/Thumb images now queue preparation before their next CPU run, after
relocation and import patching. The worker follows known entries, direct exits
and literal references in loaded executable images. Later library loads get
the same treatment; targets only discovered at runtime retain hot compilation.
This shifts work into loading, with additional startup time and code memory.
See [early compilation results](../benchmark/PRECOMPILATION_RESULTS.md).

Current compatibility, performance limits and screenshots are recorded in
[Sky Force results](../benchmark/SKY_FORCE_RESULTS.md).
