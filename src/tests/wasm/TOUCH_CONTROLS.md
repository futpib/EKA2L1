# Browser touch controls

All eight catalogue games share a floating stick, four stable action positions
and a separate **L / OK / R** row. The action names and phone keys follow each
game's original controls; unused action positions are hidden. The
[game settings audit](GAME_CONTROL_RESEARCH.md) records the inspected key lists.

The game fills the viewport with its original aspect ratio, without cropping.
Translucent controls float over the picture or available letterbox space.
**☰** opens the launcher, sound, full screen, controls, phone keypad and diagnostics.
The game keeps running while this menu is open.

| # | Game | Action buttons | Movement |
|---|---|---|---|
| 1 | Snakes | Boost, Slow, Activate, Hard Turn | Relative steering, four directions for menus |
| 2 | Sky Force | Secondary, Fire | Eight directions |
| 3 | K-Rally | Gas, Turbo, Rocket, Mine | Four directions, horizontal steering lock |
| 4 | Asphalt 2 | Turbo, Brake | Four directions, horizontal steering lock |
| 5 | Hooked On | Cast / Reel | Eight directions, ordinary press/hold/release |
| 6 | Asphalt 3 | Nitro, Brake, Drift Left, Drift Right | Four directions, horizontal steering lock |
| 7 | Tetris | Rotate clockwise, Rotate counterclockwise, Hold, Drop | Cardinal only, horizontal movement lock |
| 8 | Super Miners | Action | Cardinal only, turns without lifting |

Away from controls, the stick appears where the movement finger touches and
disappears on release.
Snakes, both Asphalt games, K-Rally and Tetris keep a gesture horizontal once it
has moved sideways. Vertical thumb drift then cannot fire a weapon or drop a
piece. Lift and start a new vertical gesture for menu navigation. Sky Force and
Hooked On allow diagonals; Tetris and Super Miners always send one direction at a
time. These are guest key presses, not finger-following character positioning.
The games' own autofire and automatic acceleration preferences are preserved.

Action buttons, L/OK/R and optional utility buttons accept touches up to 12 CSS
pixels beyond their visible rectangles. Where extensions overlap, the nearest
visible button wins. The game menu also has 12 pixels of invisible target padding.
The artwork and layout remain the same size.

New movement touches are blocked inside the action cluster (including unused
slots) and within 24 pixels of the cluster or game menu. A touch in that guard
area remains inert until lifted, even if dragged into the playfield. An existing
stick can still be dragged near controls, and a second finger can hit a padded
action target while steering. These bounds follow resized and moved controls,
including left-handed layouts.

Multiple fingers can move and press actions together. Releasing one input does
not release a key owned by another finger or keyboard key. Blur, cancellation,
dialogs, layout changes and app changes release held input.

**☰ → Touch controls** offers sizing, spacing, opacity, handedness and four
independent action mappings. **Show extra controls** adds a nonmodal utility row:
camera/look for Snakes, manual Gas/camera/lights for Asphalt 2, camera controls for
Asphalt 3. Restart stays in the phone keypad, away from normal gameplay actions.
**Move controls** lets the player drag the groups; **✓** returns to play. **Reset**
restores the game's preset. L/R retain positional names because the game decides
their meaning. OK always sends centre Select, independently of action mappings.

Asphalt 3 selects numeric bindings from its guest canvas orientation, not the
browser viewport. Its Brake and Drift Right keys differ between portrait and
Landscape 270. A guest display change releases held keys before rebinding.
The control settings also offer an explicit orientation override, and remember
separate mappings for each orientation. Arrow rotation remains the emulator's
responsibility. If action keys are remapped inside a game, match them manually
in the action selectors here.

Preferences use `eka-touch-v2:<catalogue ID>`, so N-Gage titles have independent
settings despite sharing a launcher UID. Custom software retains its launch
identity. Old layout/visual preferences migrate, while obsolete named presets'
primary keys do not override the new actions. Shared legacy N-Gage settings do
not get copied into every N-Gage title. Malformed or blocked storage falls back
to usable defaults.

Optional tap-to-hold is available for K-Rally's Gas and custom games. It starts
released and releases on focus loss, hidden pages, dialogs or layout changes.
K-Rally players can latch Gas to free their action thumb for Turbo or weapons;
remapping Gas to a weapon disables that latch.
Timed casting, weapons, rotation and drops use ordinary presses and releases.
**Phone keypad** exposes every digit, star/hash, softkeys and Clear. Enter remains
Select; Space uses the primary action. Keyboard numbers and the keypad send phone
digit scan codes 48–57, matching the native frontend.

On browsers with the Vibration API, movement touches, changes of direction and
action/keypad presses request short 8 ms haptic pulses, at most once every 40 ms.
Mouse and keyboard input do not request vibration. **Haptic feedback** can be
disabled in the control settings. Unsupported browsers omit that setting and
continue to handle input normally; physical feedback depends on the device and
browser allowing vibration.

The game keeps running while settings are open. Use the game's pause controls
before editing during play.

The controls render on input or layout changes; they add no gameplay polling loop.
JavaScript and CSS are versioned through the existing asset cache. Normal WASM
builds copy both files alongside the HTML, audio assets and runtime.

## Verification

### Larger touch targets and protected movement starts, 2026-10-23

The regression test reproduced the original problem: a touch beside the action
cluster became an Up input when dragged away. The expanded browser fixture now
passes all nine profiles in portrait/landscape and both handed layouts. It checks
12px button padding, nearest-button selection in overlapping padding, unchanged
artwork bounds, a 24px no-spawn guard, utility targets, cancellation, simultaneous
steering/actions and continued movement through the protected area. Exact mouse
coordinates also verify padding independently of Chromium's touch adjustment.

The real Snakes and Tetris routes each passed four ordinary action presses and
four touches eight pixels outside the drawn actions, with steering held. Guard
misses produced neither joystick state nor consumed guest input. The padded menu
opened successfully, and all inputs were released. Captures were reviewed during
gameplay. The first runs exposed a menu path that received pointer events without
a synthesized click; menu activation now handles pointer release directly while
preserving keyboard/assistive clicks and ignoring duplicate native clicks.
Cancelled or dragged-away menu touches remain inactive.

Evidence: `/home/claude/.scratch/eka-touch-targets-20261023/`, with the completed
game reports in `snakes2/` and `tetris2/`, `final-contract.log`, and deployment
hashes in `deployment.json`. The committed Snakes/Tetris routes include the new
near-miss cases. These are desktop Chromium touch-injection checks, not physical
phone testing or a new performance benchmark. The emulator WASM is unchanged.

Deployed at <https://claude-laptop.lan:8188/> as
`touch-targets-e5728ebd27-af0162b2`. A fresh live Snakes replay passed all nine
checks (`live-snakes/`). Served HTML asset versions, JS/CSS hashes and the unchanged
WASM hash were checked after waiting for the restarted service to listen.

### Universal action presets, 2026-10-22

The browser input-contract fixture passed before and after implementation.
Expanded coverage checks all presets, separate N-Gage settings, legacy migration,
horizontal gesture locking with fresh vertical menu gestures, orientation changes
releasing held input, independent orientation mappings, weapon latch prevention,
utility keys and 44-pixel minimum targets. All visible targets fit at 320×568,
390×844, 568×320 and 844×390 even with maximum sizing/spacing and utility controls.
Haptics and blocked-storage cases continue to pass.

All eight games were launched through their actual catalogue entries in fresh
Chromium profiles using the current deployed WASM runtime and prepared N-Gage
installations. Hardware ANGLE/Vulkan rendering and trusted CDP touch input were
used. Every preset action delivered the expected key and advanced the real
emulator's consumed-input count. Simultaneous movement/action, releasing one
finger while retaining the other, and final release were checked where relevant.
The captures were inspected to establish gameplay rather than just loaded menus.
No captured page errors, guest panics or Wasm runtime errors occurred.

| # | Game | Gameplay evidence and scope |
|---|---|---|
| 1 | Snakes | Live level; turning with Boost, Slow, Activate and Hard Turn input. |
| 2 | Sky Force | Stage 1; movement with Secondary and Fire. |
| 3 | K-Rally | Tutorial track; touch Gas reaches 61 MPH, Gas with steering, Turbo/Rocket/Mine input. Weapon effects require ammunition and are not independently established by input delivery. |
| 4 | Asphalt 2 | San Francisco race; Turbo and Brake with steering; brake lights visible. |
| 5 | Hooked On | Fishing; first tap opens cast-power meter, second casts, held action reels the line back in. No successful fish catch is claimed. |
| 6 | Asphalt 3 | Racing in Landscape 270 and portrait; all four actions in each. The game's pause-menu Display Mode changes the canvas from 320×240 to 240×320 and automatically selects the portrait bindings. |
| 7 | Tetris | Marathon board; distinct clockwise/counterclockwise piece shapes, Hold receives the piece, Hard Drop places the next piece. |
| 8 | Super Miners | Challenge level; cardinal movement plus Action input. Restart is absent from the gameplay cluster. |

These are input integration checks, not full playthroughs or performance/audio
benchmarks. Special actions without charges are checked against the game's key
list and consumed input, not claimed to have produced an unavailable effect.
Screenshot FPS values are affected by explicit guest-time stepping and must not
be compared. Physical phone comfort and vibration remain separate checks.

Evidence is under `/home/claude/.scratch/eka-controls-implementation/`: each game
has screenshots, navigation commands, browser logs and a completed `report.json`.
The final fixture log is `contract-final2.log`. Reproduce a fresh game route with:

```sh
node src/tests/wasm/game-controls-e2e.mjs tetris NEW_OUTPUT \
  https://claude-laptop.lan:8188 src/tests/wasm/game-controls-routes.json
```

The routes record the reviewed navigation and touches; screenshots still need
review if installation state or game timing changes. Browser profiles are removed
on completion. The fixture uses a mocked native input API; this eight-game runner
uses the real emulator. The WASM bytes remain SHA-256
`af0162b29b63d1c629f7b57c02ca35077c508264b712ccb5832369e16a8fa506`.
Only the HTML/JS/CSS frontend changes in this deployment.

The same frontend is deployed at <https://claude-laptop.lan:8188/> as release
`touch-3e8d713517-af0162b2`. Fresh live Snakes and Tetris replays passed all actions,
input release and both viewport layouts (`live-snakes2/`, `live-tetris2/`). The
served script hash is
`3e8d713517b63f986922cde481b95f5e25cb25e5e1d0dffb41cf9d54ee1729be`;
`deployment.json` also verifies CSS and unchanged runtime bytes. Two probes
immediately after the service restart encountered connection refused before the
server listened; they are retained separately from the completed live runs.

### Dynamic controls and haptics, 2026-10-13

The input-contract fixture passes with floating controls for every profile,
including saved fixed-pad preferences. It checks touch-origin placement,
four/eight directions, simultaneous movement/action, release ownership, layouts
and saved settings. Haptic API requests, disabling, mouse/keyboard exclusion,
unsupported APIs and thrown vibration errors are checked with an intercepted
browser API; this does not establish physical vibration on a phone.

The actual HTTPS launcher on `.lan` was checked in hardware-rendered Chromium
with trusted touch events. Snakes gameplay and Sky Force's Stage 1 introduction
received movement/action input; all eight catalogue profiles spawned the stick
at the touch location and hid it on release despite saved fixed-pad preferences.
No captured page errors or guest panics occurred. Evidence is outside Git under
`/home/claude/.scratch/eka-dynamic-controls-20261013/`.

The deployed touch script matches source SHA-256
`23f0ae61c1ddb13d90b314a29634c776738aed28cc1ac309b8c11496ff029aba`.
The WASM runtime is unchanged; only the JS/CSS input assets were replaced.

Run `node src/tests/wasm/touch-controls.test.ts` for real browser pointer routing:
sliding, multiple fingers, keyboard ownership, cancellation, neutral zones,
four/eight directions, holding/releasing actions, saved profiles, editing,
keypad access, CSS caching, malformed storage and small/landscape layout bounds.
This fixture checks input contracts; it does not replace game integration.

Run `node src/tests/wasm/game-picker.ts URL NEW_OUTPUT` for the real emulator,
both games, consumed inputs and simultaneous touch movement/action, switching,
portrait/landscape captures, settings and the custom-file launcher. Inspect the
saved gameplay captures. The report retains audio failures separately; a failing
audio check must not be described as a complete E2E pass.

### Initial control-deck verification, 2026-10-09

The controls are served at <https://claude-laptop.lan:8188/>. The browser input
contract, asset-cache and compiler-policy tests pass. Layout bounds pass at
320×568, 390×844, 568×320, 667×375, 844×390 and 1024×768, including enlarged
generic controls.

The real two-game launcher test ran on Chromium 153.0.8010.52 with NVIDIA
Quadro T1000 hardware rendering through ANGLE/Vulkan. Both games consumed touch
input, held movement and action simultaneously, retained movement when the action
finger lifted, and released every key afterward. Switching from Sky Force to
Snakes and then to the custom-file launcher passed. Portrait and landscape
gameplay screenshots were inspected: the controls remain outside the picture,
which keeps its guest aspect ratio. Sky Force advanced from 826 to 1,856 presented
frames and 10 to 20 consumed input events; Snakes advanced from 331 to 857 frames
and 20 to 28 inputs during the integration captures.

The complete E2E command still exits with failure because neither browser audio
check reports non-silent output. This is the [previously recorded audio issue](../benchmark/AUDIO_SIMD_RESULTS.md),
not a full E2E pass. There were no page errors or failed asset requests. These
checks emulate touch in desktop Chromium; physical Android/iOS testing is not
claimed.

Chromium's legacy CDP touch-injection path stopped delivering any touch events
after same-origin navigation. A two-div blank page with the same isolation headers
reproduced it without any emulator or controls code. The integration test uses
Chromium's `SyntheticPointerActions` injection route and verifies trusted pointer
events; both navigation and multi-touch then pass. This is a test-launch setting,
not a production browser requirement. The [Chromium implementation](https://github.com/chromium/chromium/blob/153.0.8010.52/content/browser/devtools/protocol/input_handler.cc)
accepts the full active-contact set for that route.

Raw reports, browser logs, blank-page probes and screenshots are retained under
`/home/claude/.scratch/eka-touch-controls/`; the final live run is
`live-native-touch/report.json`. `deployment.json` records the served asset checks.
The deployed WASM is unchanged from the preceding launcher build, SHA-256
`8ba81e0cbac677d49eaef4f651fa4e208bc4759821e8653703726facb595efbe`.
The frontend was staged with the existing runtime and verified against the source
HTML, JavaScript and CSS; no C++ rebuild or runtime-performance change is claimed.

### Visual references for the overlay revision

The presentation was compared with actual gameplay screenshots, rather than
inferring appearance from feature lists:

- [Brawl Stars](https://minireview.io/shooter/brawl-stars): translucent control rings
  over the playfield, with distinct movement and attack controls.
- [Dead Cells on iOS](https://noescapevg.com/review-dead-cells-ios-is-the-perfect-mobile-roguelike/):
  a thumbstick and action icons around the screen edges, without a separate panel.
- [Sky Force Reloaded](https://minireview.io/arcade/sky-force-reloaded) and
  [the developer's screenshots](https://www.idreams.pl/pl/our-products/show/product/77-Sky-Force-Reloaded):
  a largely unobstructed portrait playfield with minimal visible controls.

These examples guide layout and visual weight; screenshots alone do not establish
input behavior. The original Symbian games retain their key-based input and
fixed screen aspect ratio. The generic layout receives the same overlay design
and customization as the presets.

### Overlay revision verification, 2026-10-09

The browser contract checks pass, including the full viewport at 390×844, hidden
launcher during play, Sky Force gestures outside the old pad area, handedness,
editing and the existing multi-touch/storage/layout cases. Hardware-rendered
staging integration passed gameplay, movement/action ownership, switching,
portrait/landscape captures and the custom launcher for both real games. Sky Force
advanced from 823 to 1,853 frames and 10 to 20 consumed inputs; Snakes from 213 to
559 frames and 20 to 28 inputs. Gameplay screenshots were inspected.

Audio remains a limitation: both non-silent output checks failed, and this run's
Snakes mute check also failed while the existing audio handler awaited resuming
a suspended AudioContext. The full integration command remains failing; the
revision does not claim to fix browser audio.

After deployment, a separate live browser check passed both games, navigation,
menu open/close, full-viewport geometry and consumed simultaneous touch input.
A generic-profile screenshot was also captured using the real emulator.
The served HTML and asset hashes match the tested files, and the runtime bytes
and service settings were preserved. Evidence is in `overlay-staged-games2/`,
`overlay-live-smoke.json`, `overlay-deployment.json` and `overlay-unit3.log` under
`/home/claude/.scratch/eka-touch-controls/`.

### K-Rally and phone digits, 2026-10-10

The browser contract test passes the K-Rally preset, menu OK, phone-digit
keyboard/keypad routing, Space/touch ownership and saved numeric-action
migration. A fresh live LAN session reached K-Rally's tutorial without game
remapping. At 390×844, trusted touch on Gas accelerated to 65 MPH; a second
finger steered while Gas remained held, releasing steering preserved Gas, and
lifting both contacts released all inputs. Keyboard Space also accelerated.
Screenshots were inspected. The other four picker games reached gameplay in
the same deployment; details and scope are in the
[five-game launcher report](../benchmark/PRE_TOUCH_GAME_INSTALLERS.md#five-game-lan-launcher-and-phone-key-input).
