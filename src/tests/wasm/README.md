# Browser game launcher

Build `eka2l1_wasm` following the [browser build instructions](../benchmark/README.md),
then run `npm ci --ignore-scripts` and `npm run serve -- 8188` in this directory.
Choose **Sky Force** or **Snakes** and press **Play**. **Load my own files** opens
the ROM/RPKG/SIS upload controls. Each launch reloads the page and starts a fresh
session; saves are not persisted. Changing the dropdown alone does not stop play.

Direct links use `?game=sky-force`, `?game=snakes` or `?game=custom`. An optional
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

Memory translation defaults to the fixed original-index 512-entry TLB, without an ARM
scalar last-page cache. `EKA2L1_TLB_HASH` and `EKA2L1_MEMORY_CACHE` are retired
and must be removed from launch environments. See the
[cache comparison and adoption](../benchmark/MEMORY_CACHE_RESULTS.md).

Eligible ARM short blocks always use inline memory access. `EKA2L1_ARM_MEMORY`
is retired and must be removed from launch environments. The TLB/direct backend
selection below remains independent of this fixed compiler behavior. See the
[eligibility and adoption checks](../benchmark/ARM_MEMORY_ADOPTION.md).

The replay and profiling harnesses also support `EKA2L1_MEMORY_IMPL=2` for the
retained direct-memory implementation. Only values 0 (TLB) and 2 (direct) are
supported; the separate direct-policy and delayed-activation controls are
removed. See [memory implementation selection](../benchmark/README.md#memory-implementations).
Direct scalar loads/stores permit unaligned addresses: the arena checks the
whole access range, and the fallback table requires the access to fit in one
page. TLB retains its alignment checks. See the
[alignment measurements and adoption](../benchmark/UNALIGNED_SCALAR_RESULTS.md).

Run the actual browser integration check against a running launcher:

```sh
node game-picker.ts https://claude-laptop.lan:8188/ /absolute/path/to/new-results
```

It launches both games through the picker, exercises keyboard/touch, measures
guest time and frame presentations, checks non-silent browser audio and narrow
layout, switches games and opens manual loading. It retains intermediate menus,
gameplay screenshots, GPU details, browser logs and measurements. Inspect those
screenshots to confirm the intended game scenes; changing pixels alone do not
prove gameplay. Performance and audio underruns are reported, not hidden behind
a boot-success assertion.

Current compatibility, performance limits and screenshots are recorded in
[Sky Force results](../benchmark/SKY_FORCE_RESULTS.md).
