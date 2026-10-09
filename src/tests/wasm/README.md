# Browser game launcher

Build `eka2l1_wasm` following the [browser build instructions](../benchmark/README.md),
then run `npm ci --ignore-scripts` and `npm run serve -- 8188` in this directory.
Choose **Sky Force** or **Snakes**, select **Instruction counting: On / Off**, and
press **Play**. Play applies both settings in a fresh session. The running status
shows the active mode. The choice stays in the URL when switching games or
reloading, and does not affect other tabs or devices. **Load my own files** opens
the ROM/RPKG/SIS upload controls. Each launch reloads the page and starts a fresh
session; saves are not persisted. Changing the dropdown alone does not stop play.

Direct links use `?game=sky-force`, `?game=snakes` or `?game=custom`. Add
`&counting=on` for the original instruction-counting mode or `&counting=off`
for count-free execution. Without this option, the server's default applies.
An optional
third command-line argument, such as `node serve.ts 8188 Snakes`, selects an
automatic default. Without it, the page waits for a choice.

Use arrows/WASD to move, Enter/Space to select or fire, and F1/F2 for softkeys.
Click **Enable sound** for audio. The buttons below the display support touch.
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

`EKA2L1_WATCHDOG_US=2000` sets count-free execution as the launcher default.
The UI can override it for an individual session. Turning counting off uses this
request interval, or 2 ms when no interval is configured. A separate worker
requests yields; statically proved finite regions finish without polling.
Paced count-free play advances guest timers from elapsed host time, while
unpaced runs retain their event clock. Counting on restores instruction-driven
guest time. The switch therefore compares both execution and timing policies.
Count-free execution is disabled by default and incompatible
with instruction verification/diagnostics. See [watchdog results and limitations](../benchmark/WATCHDOG_RESULTS.md).
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
With verification off, these runs select direct memory and unsafe code mode 3.
Interpreter, verifier and mutation-compatible runs select TLB unless explicitly
overridden. The launcher also selects TLB for `EKA2L1_UNSAFE_CODE=0` when no
memory override is supplied. Only values 0 (TLB) and 2 (direct) are
supported; the separate direct-policy and delayed-activation controls are
removed. See [memory implementation selection](../benchmark/README.md#memory-implementations).
Direct scalar loads/stores permit unaligned addresses: the arena checks the
whole access range, and the fallback table requires the access to fit in one
page. TLB retains its alignment checks. See the
[alignment measurements and adoption](../benchmark/UNALIGNED_SCALAR_RESULTS.md).

Compiled syscalls (`EKA2L1_COMPILED_SVC=1`), sparse ROM lookup
(`EKA2L1_SPARSE_ROM_LOOKUP=1`) and outlined entry budgets
(`EKA2L1_ENTRY_BUDGET=2`) are enabled by default. Entry-budget mode 0 retains
per-span checks. These policies are frozen before initialization. Full state
pruning and IR mode 17 remain the defaults. See the
[combined measurements and graduation](../benchmark/RECOVERED_DEFAULTS_RESULTS.md).

Division lowering, entry-only pruning, static count batching (IR mode 18),
and inline entry-budget recovery (mode 1) were removed after the completed
controlled sweep. Remove `EKA2L1_DIVISION_DIGITS`, `EKA2L1_ENTRY_ONLY_PRUNING`,
and `EKA2L1_EXECUTION_LIMITS` from launch environments, even if set to zero or
the former default. The limits are now fixed: 512 source bytes, 32 leaf
instructions, 8 inline sites, and 512 regions per chain. Their read-only API
still reports `512,32,8,512`. See the [56-entry cleanup audit](../benchmark/RETIRED_SWEEP_EXPERIMENTS.md).

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

To measure one paced counting-mode combination through the launcher:

```sh
node counting-comparison.ts https://claude-laptop.lan:8188/ /absolute/path/to/new-results chrome snakes on 30
```

The last arguments select `chrome` / `firefox`, `snakes` / `sky-force`, counting
`on` / `off`, and the measurement duration in host seconds. Run combinations
serially with fresh output directories. Firefox needs working WebGL (on this
Linux host, the existing `DISPLAY` and `XAUTHORITY` are supplied). The script
uses an isolated browser profile that accepts the local HTTPS certificate.
It exercises the mode selector and Play button, checks the runtime mode and
instruction totals, and samples presentations and guest time with sound muted.
It also records installed compiled-function counts; any ARM-to-WASM translation
during the window is included in elapsed time, rather than subtracted from FPS.
Screenshots are taken outside the measurement window; inspect them to confirm
gameplay. The result is paced playability, including the selected clock policy,
not an isolated measurement of instruction-counting overhead.

Current compatibility, performance limits and screenshots are recorded in
[Sky Force results](../benchmark/SKY_FORCE_RESULTS.md).
