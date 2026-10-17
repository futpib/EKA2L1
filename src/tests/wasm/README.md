# Browser game launcher

Build `eka2l1_wasm` following the [browser build instructions](../benchmark/README.md),
then run `npm ci --ignore-scripts` and `npm run serve -- 8188` in this directory.
Choose a game from the catalogue and press **Play**.

The supported catalogue contains eight S60 3rd Edition games: Snakes, Sky Force,
K-Rally, Asphalt: Urban GT 2, Hooked On: Creatures of the Deep, Asphalt 3: Street
Rules, Tetris and Super Miners. All use the stock Nokia 5320 XpressMusic
RM-409 firmware 04.13 (Symbian 9.3, S60 3rd Edition FP2). Explode Arena and
Pathway to Glory have been retired from the launcher because their recovered
releases require earlier Symbian generations.
See the [catalogue cleanup checks](../benchmark/S60V3_CATALOGUE_RESULTS.md).

The **N-Gage** application is also available directly as `?game=n-gage`. It
installs the original Nokia-signed 1.40.1557 client already used by the N-Gage
games, and opens its local **My Games** library on the same saved phone. Existing
installations are reused. The served SISX is the unchanged signed client
component extracted from Nokia's wrapper, not a patched or re-signed package.
See the [client and persistent-library checks](../benchmark/NGAGE_CLIENT_RESULTS.md).

The browser uses WasmFS: C/C++ file operations run on their calling worker,
so file metadata, seeks and directory reads do not synchronously visit the
browser main thread. File contents occupy the WASM heap within the existing
2 GiB limit. Uploads explicitly truncate before the SDK's bulk write, including
empty replacements. See the [filesystem measurements](../benchmark/WASMFS_RESULTS.md).

The launcher keeps a virtual device in this browser using IndexedDB. Games
using the same ROM/RPKG share installed applications, C/E drives, saves,
registries and OS files. Reloading restores that device before initialization;
already installed packages are skipped. Different firmware has separate data.
The prepared image seeds an empty device once and never replaces an existing
phone's files.
On supported Chromium builds, the installed ROM copy is stored in OPFS after
installation to reduce warm WASM heap use. Other device files use the existing
in-memory filesystem and IndexedDB persistence. Firefox uses that existing
path for the ROM as well. See the [OPFS measurements](../benchmark/OPFS_ROM_RESULTS.md).

On supported Chromium builds, read-only Z-drive firmware files of at least
64 KiB also move to OPFS after installation. The layout is saved with the
device, and subsequent launches restore the links automatically.
Smaller files and writable C/E drives remain in RAM. See the measured storage
tradeoffs in
[read-only firmware results](../benchmark/READONLY_FIRMWARE_RESULTS.md).

Changes are saved asynchronously every ten seconds. **Save device** flushes
file buffers and commits a fresh disk save; **Play** closes the current emulator
and saves before switching games. The status in the game menu reports save
completion or an actual storage error. A second tab cannot write the same
device concurrently. Data belongs to this browser and site; clearing site data
removes it. Guest RAM and running processes restart on reload. Requesting the
browser's persistent-storage protection does not guarantee it is granted.

**Load my own files** opens the ROM/RPKG/SIS upload controls and reuses the
matching saved device. Changing the dropdown alone does not stop play.
The [persistent-device results](../benchmark/PERSISTENT_DEVICE_RESULTS.md)
describe the storage protocol, verification and limits.

Measure a cold browser launch through this same UI with
`node startup.mjs https://claude-laptop.lan:8188/ NEW_OUTPUT hooked-on 240`.
This harness selects a temporary disk so prior installations cannot contaminate
cold-launch results. Scripted routes are temporary too; tests can explicitly
mount a disk to measure saving overhead. Normal play uses persistence.
The output includes first-presentation timing, N-Gage installation completion
or prepared-image restoration, logs and screenshots. Prepared-image runs stop
after a nontrivial guest screen; this can still be the N-Gage client.
Inspect the screenshots: a logo or client frame is not a
playable game. See the [startup results](../benchmark/STARTUP_BOOKKEEPING_RESULTS.md)
and [compiler/admission follow-up](../benchmark/STARTUP_FOLLOWUP_RESULTS.md).
The [Thumb compiler follow-up](../benchmark/THUMB_STARTUP_RESULTS.md) measures
faster fresh installation without reducing precompilation coverage.

Direct links use `?game=sky-force`, `?game=snakes`, `?game=k-rally`, `?game=asphalt-2`,
`?game=hooked-on`, `?game=asphalt-3`, `?game=tetris`, `?game=super-miners` or
`?game=custom`.
All browser execution uses the watchdog without instruction accounting.
Legacy `counting` URL parameters are ignored and removed on Play.

The compiled-only browser CPU can also use derived ROM translations. After a
successful game route, generate a cache with:

```sh
EKA2L1_ROM_CACHE_CAPTURE=1 EKA2L1_BROWSER_MEMORY=1 \
  node arena-growth-benchmark.mjs GAME /absolute/new-output SERVER_URL
mkdir -p ../../../build-wasm/src/emu/wasm/rom-cache/GAME
cp /absolute/new-output/rom-cache.bin /absolute/new-output/manifest.json \
  ../../../build-wasm/src/emu/wasm/rom-cache/GAME/
```

Restart the development server to discover those artifacts. An optional
`EKA2L1_ROM_CACHE_DIR` selects another cache directory with the same game
subdirectories. The launcher downloads the cache alongside game assets, checks
its SHA-256 and stages it before initialization. The CPU verifies the exact
build, ABI and compiler policy, and fingerprints the whole mapped ROM backing
including all code folded into translations. Missing or incompatible entries compile on
demand. Regenerate caches after changing the WASM binary; these are generated
deployment files, not game dependencies committed to Git.

This stores optimized translated code. Guest RAM, installed files, clocks and
running processes still start normally. Export happens after FPS and memory
sampling and is excluded from their results. The public `.lan` release uses a
build-matched Asphalt 3 cache; see its validation and remaining limitations in
[compiled-only results](../benchmark/COMPILED_ONLY_RESULTS.md).

An optional
third command-line argument, such as `node serve.ts 8188 Snakes`, selects an
automatic default. Without it, the page waits for a choice.

Use arrows/WASD to move, Enter to select, Space for the configured action,
and F1/F2 for softkeys. Number keys send the corresponding phone keys.
During play, the FPS display below the menu button shows emulator presentations
per second over a rolling window of about two seconds. It updates twice per
second, includes stalls, and resets its sampling window after a background tab.
Open **☰** for game selection, sound, full screen and control settings.
Touch controls overlay the game without a permanent toolbar or control deck.
Touch uses independent movement/action areas,
with floating movement for every game, optional haptics, saved per-game layouts, handedness and an
expandable phone keypad. See the [touch control guide](TOUCH_CONTROLS.md).
Sky Force first shows a splash screen and language selection, followed by
**Start game**, difficulty and ship selection.

K-Rally uses arrows to navigate, Enter or touch **OK** to confirm, and
left/right to steer. Hold **Space**, **4** or touch **Gas** to accelerate;
**7** is turbo and up/down fire weapons. Set up a profile, then choose
**Single Player → Tutorial** to learn the controls and unlock racing. Its 1.01
package is the recovered Team SyMBiAN preservation copy; an original publisher installer was not recovered.

Asphalt asks whether to enable sound: F1 selects **Yes**, F2 selects **No**.
Enter advances past its title screen and selects **Instant Play**. Arrow keys
steer during the race. Its original N95 8GB 1.0.1 installer runs on the stock
5320. The browser build includes the MIDI instrument banks used by the Qt
frontend; without them, Asphalt failed during audio setup before the title.

Hooked On installs the N-Gage 1.40 client and imports the recovered 0.74 BiNPDA
package listed in `games.json`. Wait for installation to finish, then choose
**Start Game**. Select English and create a save slot/name on the first launch.
Choose **Instant Play** to reach Costa Rica. Without a prepared image, the
client imports the package on every fresh launch.

The Hooked On prepared-image pilot restores installed device/game files before
emulator initialization. It skips device/SIS installation and N-Gage import;
game logos, initial setup and ARM-to-WASM compilation still run. Normal launches
seed an empty saved device; subsequent launches restore its writable files.
Cold-launch tests use fresh temporary files. This is not a running-emulator checkpoint.

Create an image using a launcher with the normal installer path:

```sh
node capture-installed-image.mjs https://claude-laptop.lan:8188/ hooked-on /absolute/path/to/new-image-directory
EKA2L1_PREINSTALLED_DIR=/absolute/path/to/new-image-directory node serve.ts 8188
node installed-image.test.ts
```

The capture tool requires local source assets in `/tmp/eka2l1-serve` (or its
optional final argument). It checks their hashes, completes a clean installation,
waits for client registration, dismisses the completed installation dialogue,
then shuts down to close file handles and captures `/data` before the game is
launched. Review `installation-complete.png`. The directory contains
`hooked-on.img.gz`, `hooked-on.json` and raw capture evidence. Only the first two
are needed by the server. The server checks the image hash and exact ROM,
RPKG, game-container and runtime-package identities; mismatches fail startup.
Games with no image keep their installer path. Keep generated firmware/game
images outside Git. See the [pilot measurements](../benchmark/PREINSTALLED_IMAGE_RESULTS.md).

All bundled choices share the stock Nokia 5320 ROM/RPKG. Games are installed and
launched by UID, avoiding ambiguous captions. Sky Force is the original game's
S60 3rd Edition release, version 1.22 at 240×320, UID `0xa020d913`.
The launcher applies no game or firmware patch. The preserved Sky Force 1.22
installer itself has a third-party BiNPDA signature; it is not an authenticated
original publisher package. The [installer recovery and browser checks](../benchmark/PRE_TOUCH_GAME_INSTALLERS.md)
distinguish publisher-signed releases from modified preservation copies.

[`games.json`](games.json) records filenames, IPFS CIDs, SHA-256 digests and
sources, including Hooked On's runtime and metadata dependencies. Binaries
stay outside Git, following the existing
[individual-file IPFS convention](../benchmark/README.md#assets).
The archive member is used unchanged; only its local filename is normalized to
`SkyForce.sis`. The downloaded SISX header identifies the same package UID.
For Hooked On, the server fetches the recovered `.n-gage` container and the
N-Gage runtime directly from their recorded CIDs and checks their SHA-256
digests. The current manifest identifies the full BiNPDA preservation package;
it is not the earlier promotional trial. Prepared images are separate generated
deployment artifacts and do not replace the original installer CIDs.

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

Direct memory (mode 2) always uses contiguous process arena prefix growth.
`EKA2L1_ARENA_GROWTH` is retired; remove it from launch environments even when
set to zero. The full-arena initialization variant and runtime selector APIs
are removed. Each process reserves
a stable 64 MiB WASM address range without clearing its unused tail. The
initialized prefix starts at 4 MiB and doubles when guest chunk allocation
needs more room. No live data or native pointers move. Generated accesses use
one cached bias and limit; addresses outside the prefix and conflicting aliases
use the canonical page table. The reservation remains 64 MiB, so this targets
physical browser memory, not WASM address-space capacity. See
[measurements and limitations](../benchmark/ARENA_GROWTH_RESULTS.md) and
[graduation checks](../benchmark/ARENA_GROWTH_ADOPTION.md).

Chrome allocation diagnostics are available through `chrome-memory-probe.mjs`
and the optional `chrome-memory` argument to `arena-growth-benchmark.mjs`.
They capture V8 heap profiles per isolate and native memory samples once per
renderer. These captures are not throughput benchmarks. The
[controlled probe and actual-game findings](../benchmark/CHROME_MEMORY_PROFILER_RESULTS.md)
show that source maps can contextualize WASM entry wrappers, but Chrome does
not record C++ allocations inside existing WASM linear memory.

Eligible ARM short blocks always use inline memory access. `EKA2L1_ARM_MEMORY`
is retired and must be removed from launch environments. The TLB/direct backend
selection below remains independent of this fixed compiler behavior. See the
[eligibility and adoption checks](../benchmark/ARM_MEMORY_ADOPTION.md).

Browser replay and profiling always use compiled execution (mode 5).
Modes 0–4 are retired in the browser; the interpreter remains an offline
correctness oracle and native reference backend.
These runs select direct memory and unsafe code mode 3.
Mutation-compatible runs select TLB unless explicitly overridden. The launcher also selects TLB for `EKA2L1_UNSAFE_CODE=0` when no
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
launcher and harness environments. Their pre-init APIs and capture/profile
switches are removed. Trusted lookup still respects mapping lifetime and the
executable-byte policy; required fallback implementations and focused correctness
tests remain. Full state pruning, predicated leaves, branch/tail/literal veneers
and the former policy-7 lowering are fixed. There is no numeric IR selector.

The browser also retires verifier/census/detailed-guest-profile controls,
comparator variant 2, exclusive-access configuration switches, the optional
deferred-memory build, and the internal ROM/region/chain/RAM stage environment
ladder. Word exclusive operations use compiled calls into the existing monitor.
Loaded image entry paths compile before execution; other code compiles on demand.
The historical batch of all EUser/FntStore exports is not part of browser startup.
Deferred accesses use a separately cached precise compiled instruction. Browser
execution allocates no interpreter storage. Compiled ROM entries first discovered
by a process can be retired after its exit and rebuilt if another process needs
them. The CPU worker returns to its browser event loop between bounded batches,
allowing ordinary browser tasks and garbage collection to run. Chrome sampling,
native attribution, coarse progress reports,
and native correctness diagnostics remain. See the [nine-group retirement and
verification](CONFIGURATION_CLEANUP.md#current-retirement-2026-10-11).

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

It launches Snakes and Sky Force through the picker, exercises keyboard/touch,
measures guest time and frame presentations, checks non-silent browser audio and narrow
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

For C++ memory attribution, build with
`-DEKA2L1_WASM_ALLOCATION_PROFILE=ON -DEKA2L1_WASM_CPP_SOURCE_MAPS=ON` and serve
that frozen artifact on a private port. This option defaults off and adds no
normal-launch controls. After sourcing the Emscripten SDK environment:

```sh
python3 src/tests/benchmark/allocation_layout.py --build build-wasm --output /absolute/path/layout.json
EKA2L1_ALLOCATION_LAYOUT=/absolute/path/layout.json node src/tests/wasm/arena-growth-benchmark.mjs asphalt-3 /absolute/path/new-capture https://claude-laptop.lan:PRIVATE_PORT/ allocator-memory
python3 src/tests/benchmark/summarize_allocations.py /absolute/path/new-capture --wasm /absolute/path/frozen-build/eka2l1.wasm --map /absolute/path/frozen-build/eka2l1.wasm.map
```

`allocator-memory` collects allocator lifetimes, stacks for blocks at least
64 KiB, source-map attribution, browser PSS/smaps and Chrome allocation samples.
On Linux, a live marker and pagemap also attribute resident whole pages to large
allocations. The optional layout file identifies the interpreter object and
reads actual decoded-buffer occupancy. `EKA2L1_BENCHMARK_PREPARED=1` selects the
server's prepared installation for games that have one; this matters for Hooked
On. Diagnostic FPS is not a performance result. Turn the allocation build option
off afterward. See [the measurements and coverage limits](../benchmark/ALLOCATION_MEMORY_PROFILE_RESULTS.md).

For a whole-browser memory profile of a real game route, use the normal served
build and the `full-memory` mode. This records PSS and `smaps` for every Chrome
process, sampled JavaScript and native allocations, and a V8 JIT perf map. On
Linux, the profile also checks which labelled executable pages are resident.
Set `EKA2L1_BENCHMARK_PERSISTENT_DISK=1` to include the saved device and its
read-only firmware offload; the default transient benchmark bypasses both.
Set `EKA2L1_MEMORY_FILE_INVENTORY=1` with a persistent run to record the live
regular-file count, logical bytes, and largest files in the route report.
Keep the capture outside Git:

```sh
node src/tests/wasm/arena-growth-benchmark.mjs asphalt-3 /absolute/path/new-capture https://claude-laptop.lan:8188/ full-memory
python3 src/tests/benchmark/summarize_browser_memory.py /absolute/path/new-capture --chromium-debug /absolute/path/to/matching/chromium.debug
```

If Chrome's allocation samplers stall before `Module.calledRun`, set
`EKA2L1_CHROME_PROFILE_AFTER_RUNTIME=1` for the capture. Sampling then starts
after the WASM runtime is ready but before the game launch and derived-code
installation. The process mappings and gameplay scene are still captured;
native and JavaScript allocation samples omit the runtime's initial setup.

To stop the investigation at the first large startup RAM rise, run the burst
probe against a private or live server. `EKA2L1_BURST_MANUAL_LIVE=1` replays the
normal game-picker download path after the core is ready; it captures the first
32 MiB PSS increase over roughly 200–300 ms, the current native allocation
profile and renderer mappings. Use `EKA2L1_BURST_NATIVE_PROFILE=0` for paired
PSS runs without sampler overhead. The `clone` and `cache-cycle` values of
`EKA2L1_BURST_CACHE_MODE` replay the old and new cache-miss handoffs without
changing the server. See the [Asphalt 3 burst result](../benchmark/ASPHALT3_FIRST_MEMORY_BURST.md).

```sh
EKA2L1_BURST_MANUAL_LIVE=1 node src/tests/wasm/memory-burst-probe.mjs asphalt-3 /absolute/path/new-capture http://127.0.0.1:8302/
```

The symbol file must match Chrome's build ID. The summary reconciles physical
PSS across processes and mappings; sampled allocation stacks and JIT perf-map
sizes are separate views and must not be added to PSS. See the
[browser attribution method](../benchmark/BROWSER_MEMORY_ATTRIBUTION_RESULTS.md)
and [current sparse-backing profile](../benchmark/ASPHALT3_SPARSE_MEMORY_PROFILE.md)
for measured ownership and limitations on the deployed build.
