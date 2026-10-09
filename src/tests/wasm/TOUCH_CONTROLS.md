# Browser touch controls

The default layout is a general-purpose game controller with large independent
movement and action areas. It supports simultaneous touches, sliding between
directions without lifting, keyboard/touch ownership of the same key, and visible
pressed states. Portrait controls sit below the game; landscape controls sit
beside it. The game display scales to fit while retaining its original aspect
ratio and resolution.

Snakes starts with a fixed four-way pad. Sky Force starts with a floating
eight-way stick and a **Fire / OK** action. Other apps get a fixed eight-way pad
and **Select**. Both movement styles are available in every profile. The floating
stick starts at the initial touch and has a neutral zone; direction hysteresis
prevents small finger movements from rapidly switching sectors. These are digital
guest key inputs, not direct manipulation of a game's character position.

**Controls** opens settings for movement style, four/eight directions, size,
spacing, opacity, handedness and action-key mapping. **Move controls** lets the
player drag each group within its area; **Done moving** returns to play. **Reset**
restores the current game's preset. Settings are stored locally per app, including
apps launched from custom files. Unsupported or malformed saved values fall back
to usable defaults; blocked storage does not prevent play.

The optional action hold mode toggles on each tap. It starts released, and releases
on focus loss, hidden pages, opening dialogs, layout changes or game changes.
**Keypad** exposes the numeric phone keys, star/hash, softkeys and Clear. Softkeys
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

### Live verification, 2026-10-09

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
