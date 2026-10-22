# Browser game controls audit, 2026-10-22

The present controls expose too few actions for most supported games. A common
touch interface is practical, with bindings and movement rules selected by game.
This document proposes that interface; it does not change the deployed controls.

## Evidence and scope

All eight games in `games.json` were opened in independent Chromium sessions
against `https://claude-laptop.lan:8188/`. Their settings, key lists, help or
tutorial screens were inspected. Hooked On also completed a cast and retrieved
an item using Select. This is a control/settings audit, not a complete
playthrough, touch usability test, or performance benchmark. The FPS numbers in
the screenshots include accelerated menu navigation and must not be compared.

Source checkout: `2dd23a19b`. The live service used release
`b26148182-af0162b2`, with the existing touch implementation. N-Gage titles used
the server's prepared installations. The browser profiles were temporary and
removed afterward; the user's installed games and preferences were not changed.

Evidence directory:
`/home/claude/.scratch/eka-controls-20261022/`. Each game directory contains
screenshots, submitted navigation commands, result/status JSON and a browser
log. `routes.json` and `cmd.py` drove the existing
`arena-growth-benchmark.mjs` harness interactively. The correct asset directory
is `/home/claude/.local/share/eka2l1/assets`, not its historical `/tmp` location.
Two initial launches used the missing old location and were discarded. Two
initial interactive commands had an `eval` syntax error and were retried before
navigation. No guest panic or Wasm runtime error was found in the eight completed
sessions. Tetris's trial eventually expired, after its key settings were captured.

## What the games actually expose

Digits below mean phone keys, not PC numpad scan codes. `Select` means the centre
of the phone's directional controller; it is a distinct key from digit `5`.

| # | Game and inspected screen | Observed bindings/settings | Consequence for touch controls |
|---|---|---|---|
| 1 | Snakes: Options → Keys; Instructions | Left/right turn. Boost `5`, Slow `8`, Activate `7`, Camera `3`, Look Left `4`, Look Right `6`, Hard Turn `2`. Keys can be reassigned. Instructions describe continuous forward movement and relative turns. | The current Select action omits the useful gameplay actions. This is relative steering, not a classic four-direction grid snake. Put Boost, Slow, Activate and Hard Turn on the action cluster; retain camera/look keys in a small optional utility row. |
| 2 | Sky Force 1.22: Options | Up/down/left/right; Primary `5`, Secondary `0`, Pause left softkey. Autofire has Off/On/Switch choices, with **On selected** in this fresh session. | Keep eight-way movement. Expose Secondary directly and retain Fire `5` for manual firing/settings compatibility. Do not assume centre Select is the configured Primary. Do not label Secondary “Bomb” without testing its actual effect. |
| 3 | K-Rally 1.01: Options → Game Controls | Left/right steer; Accelerate `4`, Turbo `7`, Rockets Up, Mines Down, Pause right softkey. Acceleration and turbo were both **manual**. Separate Menu Controls and Game Controls pages; automatic acceleration/turbo are available. | Gas alone is insufficient. Add Turbo, Rocket and Mine. Prevent a sideways steering gesture from drifting into Up/Down. Automatic acceleration is an optional in-game preference, not a setting to silently force. |
| 4 | Asphalt Urban GT 2: Options; Help | Accelerator **Auto**. Help lists Turbo `2`, Turn Left `4`, Turn Right `6`, Accelerator `5`/`8`, Brakes `0`, Change View `*`, Headlights `#`. | Prioritize Turbo and Brake. Gas is useful if the player switches to manual acceleration. Camera/headlights can be optional utility buttons. Do not assign Down to Brake merely because newer Asphalt versions do so. |
| 5 | Hooked On: Options; boat and fishing tutorials | Main Options showed sound/music volumes. Directions navigate the boat/look around. Gaming Key A starts the casting meter and a second press chooses cast power. Centre Select successfully performed these actions; holding it retrieved a volcanic rock. Left/right softkeys dismiss help, pause or keep/release a catch according to the screen. | A large Cast/Reel action plus directions and both softkeys is sufficient for the observed sequence. Leave tap-to-hold disabled: choosing cast power requires distinct presses, and reeling requires controllable holds/releases. No key-remapping page was found in the main Options. |
| 6 | Asphalt 3: Options → Controls → Portrait / Landscape 270; Help | Separate orientation-specific control lists; details below. Nitro, braking, drifting and camera changes are distinct actions. | A generic Select button is inadequate. Profile by game **and guest orientation**; browser viewport orientation alone is not enough. |
| 7 | Tetris: Options → Remap Keys | Left/right move; centre Select rotates clockwise; `5` rotates counterclockwise; Down soft-drops; Up hard-drops; `0` holds a piece. Remapping is available. | Expose both rotations and Hold. Use cardinal movement, never simultaneous diagonal arrow presses. A separate Hard Drop button is useful; accidental Up while moving sideways must be prevented. `5` and Select are demonstrably not interchangeable here. |
| 8 | Super Miners 1.07: Options → Redefine Keys | Four directions, Action `5`, Restart `8`, Pause right softkey. Keys can be reassigned. | Use four directions and a visible Action button. Keep Restart out of the main action cluster to avoid accidental level loss. The gameplay meaning of Action was not exercised in this audit. |

Asphalt 3's own control lists show:

| # | Action | Portrait | Landscape 270 |
|---|---|---|---|
| 1 | Brake | `8` | `6` |
| 2 | Nitro | `2` or `5` | `5` or Gaming A |
| 3 | Steer left | `4` | `8` |
| 4 | Steer right | `6` | `2` |
| 5 | Drift left | `1` or `7` | `7` or `9` |
| 6 | Drift right | `3` or `9` | `1` or `3` |
| 7 | Next camera | `#` | `#` |
| 8 | Previous camera | `*` | `*` |

The help also describes holding Gaming B while turning. Its corresponding
browser scan code was not established here; the numeric drift bindings above
are the directly documented alternative. Both control lists were inspected;
this audit did not remap them or race-test each numeric binding.

The emulator already rotates arrow scan codes through
`window_key_shipper::start_shipping()` → `post_processing_scancode()` using
`focus->scr->ui_rotation`. Numeric keys are not rotated by that function. The
frontend must not blindly rotate arrows a second time. Exposing semantic game
actions requires validating the game's current orientation and remapping state.

## Current implementation problems

Only Snakes, Sky Force and K-Rally have named presets in
`src/emu/wasm/touch-controls.js`. Snakes gets four directions; everything else
gets eight. K-Rally's primary action is digit `4`; all others use centre Select.
The primary action selector only offers Select, `4`, `5`, `7`, `0`, `*`, `#`.
It cannot configure Snakes Slow/Hard Turn or several Asphalt actions at all.

Extra phone keys exist in a modal keypad, but movement and normal controls are
unavailable while it is open. The game keeps running. That is a fallback for
occasional input, not a usable home for Turbo, Hold, weapons or braking.

`shell.html` calls `ekaTouchControls.startGame(appName)` with the launch UID.
Hooked On, Asphalt 3, Tetris and the N-Gage client share `0x20003b78`. Consequently
they share `eka-touch-v1:0x20003b78`, rather than independent saved preferences.
The live Asphalt 3 page confirmed this key in `asphalt-3/touch-profile.json`.
Known games should use the catalogue ID; custom installs need a separate
fallback identity. Migration must not apply one old generic preference to every
N-Gage game's new preset.

The existing multi-touch ownership, release-on-blur/cancel, floating origin,
handedness, sizing and optional haptics are useful foundations. Preserve them.

## Proposed common layout

```text
                 game picture                     ☰

      floating stick                 [ L ][ OK ][ R ]
      appears at touch                       [ X ]
      and vanishes on lift             [ Y ]       [ A ]
                                             [ B ]
```

Letters indicate stable layout slots, not the labels shown to the player.
Show actual action names/icons, and hide unused slots without moving the others.
Place controls in letterbox space when available and keep their groups movable.
Keep the user's dynamic stick preference for every game. Mirror the groups for
left-handed use. The L/OK/R row remains available independently of gameplay
actions: even games that fire with `5` need centre Select for menus.

| # | Game | Suggested visible gameplay actions | Movement rule |
|---|---|---|---|
| 1 | Snakes | Boost, Slow, Activate, Hard Turn | Relative left/right steering; cardinal menu navigation |
| 2 | Sky Force | Secondary, Fire | Eight directions, simultaneous horizontal/vertical allowed |
| 3 | K-Rally | Gas, Turbo, Rocket, Mine | Keep an established sideways gesture horizontal until lift |
| 4 | Asphalt 2 | Turbo, Brake; Gas available for manual mode | Keep an established sideways gesture horizontal until lift |
| 5 | Hooked On | Cast / Reel | Directional movement/look, with ordinary press/hold/release |
| 6 | Asphalt 3 | Nitro, Brake, Drift Left, Drift Right | Sideways steering; orientation-specific action bindings |
| 7 | Tetris | Rotate clockwise, Rotate counterclockwise, Hold, Hard Drop | Cardinal only; lock established sideways gestures until lift |
| 8 | Super Miners | Action | Cardinal movement, allow intentional turns without lifting |

Horizontal gesture locking solves a specific ambiguity: after the player starts
steering/moving sideways, moving their thumb diagonally must not fire a rocket
or hard-drop a piece. A fresh vertical gesture still navigates menus using
Up/Down. This avoids guessing game/menu state from screenshots, delays or the
last key pressed. It does not eliminate all mis-input; thresholds and usability
need real phone testing. A future stricter gameplay-only axis restriction would
need an explicit mode or reliable guest-state signal, because menus still need
vertical navigation.

Keep camera/look/headlight controls in an optional nonmodal utility row, so they
can be used with steering. Keep destructive Restart in a separate menu. The full
phone keypad remains available for custom software and text entry.

Preserve the games' native autofire/automatic acceleration settings. Do not
replace them with a JavaScript repeat timer or silently change the guest's
options. Hold-latching should remain opt-in and disabled for timed casting,
rotation/drop actions and weapons. Haptic pulses should acknowledge deliberate
input, not every frame or guest key-repeat event.

L/R should retain their positional names: Sky Force uses left for Back on some
screens, while other games use right. They cannot universally mean Confirm and
Cancel. Opening emulator settings must also not be presented as pausing the
game; the current implementation does not pause it.

## Implementation and validation order

1. Separate catalogue identity from launch UID. Replace the single action field
   with per-game action lists covering all phone keys and arrows. Preserve input
   ownership and release semantics; include held-button migration tests.
2. Populate the eight presets from the captured settings. Use cardinal input by
   default and eight-way input where it is useful. Add horizontal gesture locking
   as a profile rule; do not infer whether a game is in a menu.
3. Establish Asphalt 3's guest orientation and live input mapping before
   shipping its action preset. Include the user's ability to change in-game
   bindings: defaults are verified here, arbitrary remaps are not detectable
   merely from the game ID. An explicit frontend remap remains necessary.
4. Exercise actual touch on the served games: Snakes turning plus each action;
   Sky Force movement plus Secondary/manual fire; K-Rally Gas plus steering and
   Turbo/weapons; both Asphalt games steering plus braking/nitro/drift; Hooked On
   timed casting/reeling; Tetris both rotations, hold and drops; Super Miners
   direction plus Action. Check menus, simultaneous fingers, blur, orientation
   changes and per-game persistence. Phone comfort remains a separate test from
   desktop Chromium touch injection.

## Selected captures

Paths below are relative to the evidence directory above.

| # | Evidence | Capture |
|---|---|---|
| 1 | Snakes full key list | `snakes2/keys.png` |
| 2 | Sky Force bindings and autofire | `sky-force2/options.png` |
| 3 | K-Rally bindings and manual/auto choices | `k-rally/game-controls.png` |
| 4 | Asphalt 2 automatic accelerator and key help | `asphalt-2/options.png`, `asphalt-2/help.png` |
| 5 | Hooked On options, casting instructions and retrieved item | `hooked-on/options.png`, `hooked-on/fishing-help.png`, `hooked-on/reel-held.png` |
| 6 | Asphalt 3 orientation-specific bindings | `asphalt-3/controls-portrait.png`, `controls-portrait-tail.png`, `controls-landscape.png`, `controls-landscape-tail.png` in that directory |
| 7 | Tetris complete key list | `tetris/remap.png`, `tetris/remap-tail-0.png` |
| 8 | Super Miners key list | `super-miners/keys.png` |

The [archived K-Rally publisher manual](https://manualzilla.com/doc/7026975/infinite-peripherals-k-rally-game-user-s-manual)
also describes separate menu/game remapping and automatic acceleration/turbo.
The installed game's settings above are the source for the concrete bindings;
controls in other ports or newer Sky Force games must not be substituted.
