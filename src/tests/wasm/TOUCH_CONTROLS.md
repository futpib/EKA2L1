# Browser touch controls

The game occupies the viewport and retains its original aspect ratio without
cropping. Translucent thumb controls float over it or in the available letterbox
space; the layout does not reserve a control deck or persistent toolbars.
**☰** opens the launcher, sound, full screen, controls, phone keypad and diagnostics.
The game keeps running while this menu is open.

Snakes starts with a fixed four-way pad. Other apps get a floating eight-way stick
and a Select action. Sky Force also uses eight directions, but its movement area
covers the play surface and the stick only appears while touching it. Its action
button maps to Fire / OK. Both movement styles remain available in every profile.
The floating stick starts at the initial touch and has a neutral zone; direction
hysteresis prevents small finger movements from rapidly switching sectors.
These controls send the original guest keys. In particular, Sky Force steering
is a floating digital stick, not finger-following ship positioning.

The input layer supports simultaneous movement/action, sliding between directions
without lifting, keyboard/touch ownership of the same key, and visible pressed
states. The small L/R buttons send the original left/right softkeys.

**☰ → Touch controls** opens settings for movement style, four/eight directions, size,
spacing, opacity, handedness and action-key mapping. **Move controls** lets the
player drag each group within its area; the onscreen **✓** returns to play. **Reset**
restores the current game's preset. Settings are stored locally per app, including
apps launched from custom files. Unsupported or malformed saved values fall back
to usable defaults; blocked storage does not prevent play.

The optional action hold mode toggles on each tap. It starts released, and releases
on focus loss, hidden pages, opening dialogs, layout changes or game changes.
**Phone keypad** exposes the numeric phone keys, star/hash, softkeys and Clear. Softkeys
are labelled by position because their meaning is controlled by the guest game.
Enter remains Select; Space uses the configured action key.

The game keeps running while settings are open. Use the game's pause controls
before editing during play.

The controls render on input or layout changes; they add no gameplay polling loop.
JavaScript and CSS are versioned through the existing asset cache. Normal WASM
builds copy both files alongside the HTML, audio assets and runtime.

## Verification

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
