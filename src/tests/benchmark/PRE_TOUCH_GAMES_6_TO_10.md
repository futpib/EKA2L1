# Keypad Symbian: games six through ten

Recovery and browser checks started on 2026-10-11. The Buzz gallery order is
Asphalt 3: Street Rules, Tetris, Super Miners, Explode Arena, and Pathway to
Glory. This is the gallery's approximate popularity order, not sales data.
Prefer the newest recoverable official release for the appropriate platform;
version numbers from different handset ports are not directly comparable.
Game and firmware binaries stay outside Git. Launcher assets are identified
by individual CIDs and SHA-256 hashes in `../wasm/games.json`.

## Tetris

The original Nokia promotional disc preserved at
<https://archive.org/details/nokia-n-gage-the-force-unleashed-2008> contains
**Tetris 4.21.41** for N-Gage 2.0. Both its metadata SIS and game SIS retain
valid Nokia controller signatures; all 24 described payload hashes match.
The game was packaged on 2008-04-07. Verification uses the embedded certificate,
not independent certificate-chain trust validation. No later official build
was recovered in this search.

The launcher uses the unchanged `.n-gage` container and original N-Gage
1.40.1557 runtime. The separately available game SIS is the exact decoded MIME
part, not a repackaged installer. The client labels the game TRIAL; this check
does not establish full-game activation.

Two fresh Chromium 153 sessions reached Marathon gameplay, including one
through the normal `https://claude-laptop.lan:8188/?game=tetris` picker. Falling
blocks, movement, rotation and dropping were checked. Let installation finish,
choose Start Game, and resume using Back to Game if the client stays foreground.
The first-play tutorial uses the left softkey to advance. Sending Cancel while
the importer is busy aborts installation; a fresh launch completed when left
undisturbed. One captured pause-menu transition has scrambled labels; gameplay
and settled board text were legible. This is not a full playthrough or an audio
verification.

Tests used the stock Nokia 5320, paced count-free execution, default direct
memory, and WASM SHA-256
`7e8d4ef829fc1d3f84d266941f105e8dcf64be68b6f504298b1dc27c7da1fd26`.
Evidence is under
`/home/claude/.scratch/eka-next-five-20261011/{tetris-e2e,lan-tetris}/`.

## Super Miners and Explode Arena

**Super Miners 1.07b** is the newest recovered S60v3 keypad branch (SIS version
1.7.2). The [publisher page](https://www.idreams.pl/super_miners.html) no longer
provides a usable installer. Searches of the old publisher/download links and
preserved collections did not recover an authenticated official S60v3 package.
The selected [preservation collection](https://archive.org/details/dertz-part-5-s-z)
copy has a DeFconX certificate whose retained signature does not verify. All
described payload hashes match. A separately recovered 1.07 demo carries a
publisher-named certificate but also fails signature verification; it is not
treated as an authenticated original. The selected 1.07b uses the full 240x320
display, whereas that demo uses a smaller 176x208 area.

**Explode Arena 1.1** (SIS 1.10.0) is an unsigned S60v1/v2 installer from the
[limowski preservation collection](https://archive.org/details/limowski),
originally named `InfiniteDreams.Explode.Arena.v1.1.S60.SymbianOS.Retail-oWnPDA.sis`.
No authenticated original keypad installer was recovered. The publisher's
[remaining page](https://www.idreams.pl/explode_arena.html) has no usable download.
Higher-numbered S90/UIQ builds are different touchscreen ports. The browser
selects the existing stock N70 ROM/RPKG for this game; the other games retain
their own device selection. No firmware or game bytes were edited.

Fresh Chromium 153 sessions through the normal LAN picker reached Super Miners'
challenge tutorial, dismissed its dialogue and moved the character left.
Explode Arena reached Arcade gameplay and moved the character left. These are
compatibility/input checks, not full playthroughs, audio verification, or
controlled FPS measurements. Their default paced clock has no instruction
accounting. Evidence: `lan-super-miners/` and `lan-explode-arena/` under the
scratch directory above. Tested WASM SHA-256:
`2bb26562423b6ea0d7269385ef9e3f2cb106b312cdbc623b489e7227e2c19bc4`.

## Asphalt 3: Street Rules

The same Nokia promotional disc contains the original **1.2.7** release, packaged
on 2008-03-19. Both Nokia SIS controller signatures verify, along with all 42
game and five metadata payload hashes. On the stock 5320 with N-Gage 1.40.1557,
importing this original fails during registration with Error -1; the reference
TLB memory path produces the same failure. A separate original-client 1.10.1318
attempt also failed to reach the game. The signed original remains preserved
outside Git; it has not been edited or relabeled as the working package.

Following the approved fallback rule, the launcher uses the recovered
`Asphalt3_fixed_by_BodyZ.n-gage` from the
[collection's Fixes and cracks directory](https://archive.org/details/Dance.Fabulous.nGage).
Its game still reports 1.2.7, but the installer is unsigned and three game files
differ from the original: `asphalt3.exe`, `trackobj.bar.ngdat`, and
`achievements.xml`. The XML changes the Spanish language tag and adds a UTF-8
BOM; that alone has **not** been established as the original's failure cause.
The separately served SIS contains exactly its embedded game installer, without
MIME framing or trailing metadata. The launcher installs the unchanged recovered
container using the original 1.40.1557 runtime. Its client tile says TRIAL;
this check does not establish campaign or full-game activation.

Accelerated Chromium 153 reached Instant Play through the normal LAN picker,
accelerated to 117 km/h and steered, with no captured page errors, access
violations or guest panics. The landscape sound prompt uses the right softkey
(F2) for No; wait for imports and loading transitions before giving input.
Evidence: `lan-asphalt-3/race-input.png`, `report.json`, `steps.log`, and
`browser.log` in the scratch directory above. Tested WASM SHA-256:
`e91b2682f0a4aca80021850afc699a1c8b1e2922b099b840deae4ff2f3d83c10`.
This is a gameplay/input check, not an FPS benchmark or complete playthrough.

The later [Asphalt 3 audio check](ASPHALT3_AUDIO.md) enables sound in both Qt and
WASM and verifies racing with continuous output through their actual audio devices.

## Pathway to Glory

The recovered retail release is **1.03**, from the
[Nokia N-Gage preservation set](https://archive.org/details/NokiaN-Gage201808).
Its original memory-card layout is retained in the 52,183,131-byte ZIP; all 125
archive entries pass their CRC checks. `game.id` identifies version 1.03 and
`game.lic` identifies `PtG 1.03-111304-4504THR`. This is a catalogued retail card
image, not a cryptographically authenticated publisher download. No later
retail release was recovered; a separately listed 1.2.3 preview is a different
pre-release. There was no official SIS release of this original N-Gage game.

The browser mounts the unchanged card on E using the same ZIP mount and AIF
registration logic as Qt. It selects a matching **N-Gage QD developer ROM**, an
unchanged extraction from <https://archive.org/download/Ngage/Ngage.zip>.
The ROM identifies itself as RH-29, G04.10 Game Developer SW Variant; the
preservation package's RH-4 label is incorrect. This is **not retail firmware**.
Trying the card on the N70 produced KERN-EXEC 3. Neither game nor firmware
bytes were patched to make this browser launch work.

Compatibility fixes were in the emulator:

- Select the app-list server by Symbian version, support ROM-only installation,
  and expose the existing card mount/registration path in the browser frontend.
- Deduplicate translated entries across old ROM image headers; duplicate
  FntStore exports otherwise invalidate the combined WASM module.
- Register the existing local Bluetooth link-manager protocol in WASM. The
  game queries its local device name even in single-player; network discovery
  and multiplayer are not enabled by this change.
- Supply a PCM WAVE backend when FFmpeg is unavailable. The game initializes
  its player with a valid empty 8 kHz mono 16-bit WAVE buffer; rejecting it
  previously stalled startup. The backend also decodes nonempty 8/16-bit mono
  and stereo PCM, supports seeking/repeat and rejects malformed input.
- Convert guest 12-bit 0RGB pixels to RGBA before WebGL upload. Desktop GL's
  texture swizzle is unavailable in WebGL; without conversion, white text and
  the sepia artwork appeared cyan. A native Qt frame dump confirmed the
  intended colors.

Accelerated Chromium 153, launched through the normal LAN picker, reached the
first Pantelleria mission. After dismissing the movement tutorial, moving the
cursor and pressing 5 made Aaron Wright walk to the selected location, reducing
his action points from 20 to 16. The game uses the left softkey (F1) through
campaign setup and 5 for battlefield actions, so its touch preset defaults to
5 while keeping a separate menu-select button. Space follows that action
mapping; 7 opens the game's own menu. Evidence is in
`lan-pathway-to-glory-color/`, including `tutorial-next.png`,
`soldier-moved.png`, the input sequence and a report with no captured page
errors, access violations or guest panics. This is a first-mission check, not
a full campaign playthrough.

A fresh run on `build-final/` repeated the mission route and used a real CDP
touch event on the default 5 button to move Allen, reducing action points
from 15 to 7. See `lan-pathway-to-glory/touch-soldier-moved.png`, `report.json`
and `pathway-final-route.txt`. No captured page errors, access violations or
guest panics occurred. An intervening run had inconsistent confirm/cancel and
directional response in gear menus (`lan-pathway-menu-input/`); its cause is
unresolved. In the successful route, returning from gear setup to the squad
screen and proceeding again reached the mission. These captures establish
working gameplay and touch action, not completely reliable menu navigation.

## Final validation and artifact availability

The normal LAN picker now contains ten games. Explode Arena selects the stock
N70; Pathway selects the developer N-Gage QD described above; the other games
retain the stock 5320. Device selection follows the game without a firmware
override or manual upload. All 17 distinct game/runtime/device assets referenced
by `games.json` were checked against both SHA-256 and recomputed CIDv1 values.
These new CIDs identify local cache files; remote pinning has **not** been
performed. Exact recovery URLs are recorded in the manifest. No recovered
game or firmware binaries were added to Git.

The final LAN service serves the immutable build at
`/home/claude/.scratch/eka-next-five-20261011/build-final/`, WASM SHA-256
`dec37577f64f2b6addc7d3d1d9e877fe4bcd288bbc0b39022fd4c22236130f63`.
Before the Pathway touch preset was added, accelerated Chromium 153 verified
Snakes' normal picker launch, gameplay and a left turn with the same runtime
fixes (`build-color/`, WASM SHA-256
`8dc897658c94e247f609dea73ed79f6004d676a7d436a7cab0c616b9e468fc95`).
The earlier per-game captures above identify their tested builds.
These checks used paced execution, the 2 ms watchdog, default
direct memory and no instruction accounting. They are not FPS benchmarks.

The final build passes the browser API/resource/startup/shutdown checks.
Compiler-policy, asset-cache and touch-control test files pass. Native audio
tests pass 175 assertions across 11 cases (including the new WAVE tests), and
the existing card-mount tests pass 14 assertions across four cases. Evidence
and logs are under the scratch directory above. Game audio output, multiplayer,
long playthroughs and commercial activation were not established by these
compatibility checks.

## Version recheck and pinning transfer

A follow-up check found no newer recovered release of the selected ports.
This is not proof that no later publisher build ever existed. Super Miners
1.07c is listed for Symbian 6.x/7.x/8.x by the
[historical distributor](https://hpc.ru/soft/softlist.phtml?t=dvl&u=3505);
the selected 1.07b is the S60v3 port. Higher version numbers on the Explode
Arena S90/UIQ ports and Pathway's 1.2.3 preview likewise do not supersede the
selected keypad S60 and retail card releases.

The `Dance.Fabulous.nGage` archive's `Games/Full/Tetris.n-gage` was downloaded
and inspected separately. Its game SIS still reports 4.21.41, with a
2008-05-13 packaging date and a BiNPDA signature. Its metadata retains the
Nokia signature. It does not establish a newer official version than the
Nokia-signed 4.21.41 used by the launcher.

At the user's request, all eleven selected game, extracted-installer,
runtime and firmware files were put into a single transport ZIP for
magic-wormhole. Each extracted member's SHA-256 matched the cached asset. Pin the individual members: the outer transport
ZIP is not a launcher asset and its CID is not used. The receiver confirmed
the completed wormhole transfer. Remote IPFS pinning has not been verified.

## Paced gameplay FPS follow-up

Fresh normal LAN picker sessions on the final WASM hash above reached gameplay
for all five games again. Chromium 153 used hardware WebGL through ANGLE/Vulkan
on the NVIDIA Quadro T1000 Max-Q, with an i7-10875H host. Runs were serial,
with default paced count-free execution, 2 ms watchdog, direct memory 2 and
immutable-code mode 3. Other desktop workloads remained running. Browser audio
output was disabled. These are single scene samples, not isolated comparative
benchmarks or maximum unpaced throughput.

FPS is the guest-presentation counter delta divided by elapsed browser time.
Loading and menus are excluded; start and end screenshots were inspected.
Guest time advanced at approximately 1x throughout each accepted sample.

| # | Game | FPS | Window | Scene and limits |
|---|---|---:|---:|---|
| 1 | Asphalt 3 1.2.7 | 31.6 | 30.24 s | Instant Play race; car reaches a wall and is stationary at the end. |
| 2 | Tetris 4.21.41 | 28.4 | 30.38 s | Marathon level 1, falling blocks and score progression. |
| 3 | Super Miners 1.07b | 32.0 | 30.22 s | Challenge level 1 after movement, timer running. |
| 4 | Explode Arena 1.1 | 38.1 | 15.21 s | Arcade level, enemies active and player alive at both endpoints. |
| 5 | Pathway to Glory 1.03 | 40.0 | 30.16 s | Pantelleria battlefield waiting for the next action after Brown moved, spending six action points. |

The initial Explode Arena 30-second sample ended at the main menu and was
rejected. The accepted shorter sample stayed in gameplay. Remaining compiler
translation/emission/installation totals were 35.3 ms for Asphalt, 3.9 ms for
Tetris, 0.2 ms for Super Miners, 11.1 ms for Explode Arena and 3.8 ms for Pathway;
these small amounts are included in the reported windows.

Asphalt's keyboard softkey did not consistently dismiss its sound prompt;
the normal on-screen right softkey did, followed by a successful race launch.
Its guest log also reports failed target-audio-format operations, so this is
not an audio-correctness result. Pathway reached the mission without the
earlier gear-menu detour on this run; that does not resolve the previously
observed input inconsistency. Tetris and Asphalt still show TRIAL in N-Gage.

Machine-readable results and raw-evidence paths are in
[`pre-touch-6-to-10-paced-results.json`](pre-touch-6-to-10-paced-results.json).
The scratch harness is `fps.mjs` under the recovery directory above. No
emulator code, live configuration, or game/firmware bytes changed for these
measurements.
