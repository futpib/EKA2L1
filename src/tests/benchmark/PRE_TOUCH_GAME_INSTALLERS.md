# Keypad Symbian: top-five installer recovery

Checked on 2026-10-10 against `wasm-port`, using the stock Nokia 5320 ROM/RPKG.
This covers the first five entries of the Buzz keypad gallery: Snakes, Sky
Force, K-Rally, Asphalt: Urban GT 2 and Hooked On: Creatures of the Deep.
The gallery's order is approximate popularity, not verified sales ranking.

The [manifest](pre-touch-game-installers.json) records exact source URLs,
versions, dates, SHA-256 hashes, individual IPFS CIDs, signature checks and
browser observations. Installers and full screenshot/log evidence stay outside
Git. The transfer contains only the five selected game SIS files. The recorded
CIDs identify file contents; they do not claim that a remote pinning service
already stores them.

| # | Game | Selected recovered package | Provenance | Browser result |
|---|---|---|---|---|
| 1 | Snakes | 0.6.0.20, S60v3 | Original Nokia installer | Gameplay and directional input verified |
| 2 | Sky Force | 1.22c, S60v3 240x320 | DeFconX preservation copy; original keypad installer not recovered | Gameplay and directional input verified |
| 3 | K-Rally | 1.01, S60v3 240x320 | Team SyMBiAN preservation copy; original installer not recovered | Tutorial track, acceleration to 65 MPH and steering verified |
| 4 | Asphalt: Urban GT 2 | N95 8GB 1.0.1, packaged 2008-02-26 | Original Gameloft-signed S60 port | Race and steering verified after restoring the browser's missing MIDI banks; see follow-up below |
| 5 | Hooked On: Creatures of the Deep | 0.74, original `.n-gage` container | Original Nokia-signed game and metadata | Original trial reaches boat map and fishing/casting scene through N-Gage 1.40; slow in this smoke run |

“Selected” means the newest relevant-platform build recovered in this search,
not proof that no later release ever existed. Modified copies are included
only where an original relevant-platform package could not be recovered.
Hooked On's original trial worked, so its recovered cracked alternative is
not included. No game binary or firmware was patched during testing.

## Recovery and authenticity

**Snakes:** the existing [official release recovery](SNAKES_RELEASES.md)
identified Nokia's `snakes60all.sis`, with executable build 0.6.0.20 and SIS
package version 1.0.0. Its Nokia controller signature and all 104 described
payload hashes verify. This test establishes browser gameplay for that newer
build; it does not replace the existing 0.6.0.19 benchmark/default asset.

**Sky Force:** [the preserved download set](https://www.myabandonware.com/game/sky-force-vxs)
and several community/Internet Archive collections yielded third-party S60v3
installers. The selected 1.22c package has a valid DeFconX self-signature, not
an Infinite Dreams signature. A later 1.32 package does have a valid Infinite
Dreams signature, but it is the S60v5 touchscreen port. It is kept separately
as `official-touchscreen-extra`, and failed before gameplay on the 5320 with
`KERN-EXEC 3`, PC `0x2`, LR `0x70009160`, and a write at zero. It is not the
answer to “latest keypad Sky Force.”

**K-Rally:** [the recovered 1.01 download](https://www.myabandonware.com/download/pcwv-k-rally)
is a third-party package despite its publisher vendor string. Its Team SyMBiAN
certificate identifies a cracker; the retained controller signature fails
verification. All 12 described payload hashes match that controller, which
checks internal consistency but does not establish publisher authenticity.
Other recovered copies were unsigned or signed by third parties too.
The package reports SIS version 1.1.0; the title screen displays 1.01.

**Asphalt:** all three recovered [S60 packages](https://www.myabandonware.com/game/asphalt-urban-gt-2-ms3)
have valid Gameloft controller signatures and matching payload hashes. The
N95 8GB build is dated 2008-02-26; the N95 build is dated 2007-05-30 and the
N91/3250 build is dated 2007-03-09. The N91 package's version 1.2.0 is a
different handset branch, not a reason to replace the later N95 8GB 1.0.1.
These are S60 ports of the game. The original N-Gage card edition is not a SIS.

**Hooked On:** the original 0.74 container was recovered from a
[preserved Nokia promotional CD](https://archive.org/details/nokia-n-gage-the-force-unleashed-2008).
It contains retailer XML, a signed metadata SIS and a signed game SIS. The
metadata's Nokia signature and both game-controller Nokia signatures verify;
all 5 metadata and 59 game payload hashes match. The game SIS is dated
2008-03-20, and the metadata SIS 2008-03-24. Both extracted SIS files are exact
decoded container parts, not repackaged installers. Keep the complete
`.n-gage` file for normal installation, or reconstruct it from those exact
parts as the browser launcher now does.

The recovered N-Gage 1.40.1557 wrapper and its inner installer also retain
valid Nokia signatures. The inner SIS is extracted unchanged from wrapper
payload 28. Its controller describes 449 checked file records; the package
contains 803 payload units, including language alternatives. These are not
803 independently signature-verified file descriptions.

Signature verification here uses each package's embedded certificate and
checks its described payload hashes. It is not independent certificate-chain
trust validation. In particular, a valid self-signature and a publisher name
in a vendor field do not turn a modified installer into an official original.

## Browser execution and the N-Gage installation fix

Runs used the real WASM shell in Chromium 153.0.8010.52, headless with Vulkan,
fresh browser filesystems and unchanged stock Nokia 5320 assets. Defaults were
paced watchdog execution without instruction accounting, AOT level 5, compiler
policy 7 and direct memory mode 2. One additional Asphalt run used TLB mode 0.
The manifest records the ROM/RPKG and both tested WASM hashes.

The first tests used `a7d35c108`. A fresh device had machine UID zero until
`devices.yml` was reloaded. Original N-Gage installers consult the machine UID
and silently skipped their device-gated application files on that first run.
Commit `2157bcbb3` moves the existing known-device UID fallback to device
creation, preserving explicit identities and leaving unknown models unknown.
The stock 5320 now reports `0x2000da5a` before its first SIS install.

Validation: WASM build passed; the native device tests passed **168 assertions
in 11 cases**; a fresh browser install of the original N-Gage client reached
its home screen and imported the original Hooked On package successfully.
Sky Force 1.22c, K-Rally, the final Asphalt retry and Hooked On use this fix.

For Hooked On, install `N-Gage-1.40.1557-inner.sisx`, place the original game
container in `E:\n-gage\`, then start N-Gage UID `0x20003b78`. Let its normal
importer install the game, select **Start Game**, then **Try Game**. Select
English, acknowledge the trial notice using the left softkey, and select
**Instant Play**. The browser check reached the boat map, moved the boat,
entered fishing and activated the casting control. Full-game activation and
online services were not tested. Installing the extracted metadata and game
SIS files directly did not register the game in N-Gage's My Games list.

The normal browser picker now offers **Hooked On: Creatures of the Deep
(trial)** at `?game=hooked-on`. Its launch installs the signed client and puts
the game container in `E:\n-gage\` automatically. The server fetches the game
SIS, runtime SIS and metadata SIS using individual CIDs in
[`games.json`](../wasm/games.json). It reconstructs the original multipart
container and requires SHA-256
`991ad3fea19a2c93ba0d39713fe68e6acb24df17991691b1963756ac96541678`,
also checked byte for byte against the CD download. No altered game SIS,
firmware patch, N-Gage registration bypass or full-game activation is involved.
Each launch still uses a fresh filesystem, so Nokia's import step repeats.

The follow-up launcher check used a fresh Chromium 153 profile against
`https://claude-laptop.lan:8188/`, selected Hooked On in the normal picker, and
used the production launch path without harness-supplied installs or filesystem
patches. Nokia's importer completed; the original trial reached Costa Rica,
boat movement, the fishing scene and its casting control. The WASM SHA-256 remains
`8177f5d6107093bc98fcc445e91a7d3118a316b54a06562f065a479b01cb0dca`;
the launcher changes are HTML/TypeScript and asset configuration.
Build and compiler-policy tests passed. The existing `game-picker.ts` regression
check passed Snakes/Sky Force gameplay, keyboard/touch, layout, switching and
manual loading, with no page/request errors. Its overall result was **failed**
because neither browser audio device advanced: Chromium reported
`The AudioContext encountered an error from the audio device or the WebAudio renderer.`
The PCM queues did advance. Audio output is not verified by this run.
Screenshots and logs are retained under
`/home/claude/.scratch/eka-hooked-launcher-20261010/{e2e,regression}/`.

K-Rally's saved controls map acceleration to **4**, turbo to **7**, steering to
left/right, and weapons to up/down. Driving was verified with guest scan code
52 for 4. The browser phone keypad currently emits numeric-keypad scan 140,
which did not accelerate this game. Remap Accelerate in the game's own
**Options > Game Controls** to a supported browser action key for normal play.

Snakes gameplay was about 16 presentations/s in a short paced sample;
Sky Force was around 32; K-Rally showed roughly 16–32 across the driving/menu
checks. Hooked On's boat/fishing scenes
were roughly 3–8 presentations/s during this smoke. These runs overlapped
and included loading/input work, so they are **not a controlled performance
comparison**. No full playthrough or audio correctness claim is made.

Asphalt initially failed with all three signed handset builds, with the TLB fallback,
and again after the device UID fix. The fix therefore addresses N-Gage
installation but does not resolve the Asphalt fault. A “Running” status or a
nonzero presentation count alone was not counted as successful gameplay.

## Asphalt browser MIDI fix

The current baseline reproduced `Failed to load SF2 bank:` with an empty path,
followed by `User::Leave(-5)` and `KERN-EXEC 3` reading `0x37e8`. The WASM
frontend omitted Qt's MIDI bank configuration, and its data bundle omitted both
default instrument banks. The fix packages the existing SF2/HSB resources and
passes their configured paths to the clocked audio driver. The original signed
SIS and stock 5320 ROM/RPKG are unchanged.

With the fix, Asphalt passes audio setup, offers its sound prompt and reaches
**Instant Play** racing with steering. The normal picker now includes it at
`?game=asphalt-2`, using its existing individual SIS CID. The API smoke test also
checks the packaged banks against the source hashes, to catch missing or stale
data bundles. This fixes the observed startup path; it does not establish that
all guest exception-unwinding failures are resolved.

Baseline and first corrected run:
`/home/claude/.scratch/eka-top5-20261010/asphalt-{current-baseline,soundbank}/`.
The corrected WASM SHA-256 is
`e1d7b25d767e83f7f045ab7cdbb2a39d82b3259fa5ef5e8f9387990ffc376f1c`.

A fresh Chromium 153 profile then selected Asphalt through the production
`.lan` picker and repeated sound-enabled startup, **Instant Play**, racing and
left/right steering. No page errors, bank-load failures or guest access
violations were observed. The browser received nonzero synthesized PCM
(4,512,379 nonzero channel samples, peak 6,553); hardware audio output was not
part of this check. Build, API/resource smoke and compiler-policy tests passed.
The Snakes gameplay regression also passed, with 161 presentations in 10.06
seconds. This is a compatibility check, not an Asphalt performance comparison.
Final LAN evidence is in
`/home/claude/.scratch/eka-asphalt-fix-20261010/{e2e,snakes-regression}/`.

## Files for pinning

The final transfer is five individual `.sis` files: Snakes, Sky Force, K-Rally,
Asphalt: Urban GT 2 and Hooked On. Local filename normalization does not change
their bytes. The larger recovery archive and ancillary evidence are not needed
for this transfer. Hooked On's runtime and metadata remain separate automatic
launcher dependencies; its game SIS alone is not a standalone N-Gage installer.

The manifest supplies each file's expected CIDv1 and SHA-256. A pinning service's
chunking/import settings may produce a different CID for identical bytes, so
use SHA-256 to check the downloaded file. No recovered game, runtime or firmware
binary is committed.
