# WASM executable-byte default, 2026-10-01

The user explicitly selected mode 3 as the normal WASM and browser default,
accepting incompatibility with games that modify executable code at runtime.
This supersedes the diagnostic-only disposition of the earlier unsafe cost study.
Both removals are selected: primary/dependency byte comparisons and generated
code-write overlap guards, entry-proof code overlap checks and mutation tracking.
There is no replacement tracking, periodic scan or automatic fallback validation.
Ordinary mappings, lifetimes, permissions, faults, budgets, interrupts and guest
scheduling remain unchanged. Native execution retains its existing default.
Explicit `EKA2L1_UNSAFE_CODE=0` restores mutation compatibility before startup.

## Local verification

All 175 compiler tests pass. The suite first verifies the WASM runtime default is
3, then explicitly selects mode 0 for its mutation-sensitive tests; the separate
42-check unsafe fixture verifies the intentional stale-code semantics of modes
1/2/3. The newly built mode-3 fault matrix matches 40,640 native comparisons.
Frontend tests cover defaults, explicit mode 0, mode readback, missing APIs,
rejected configuration, wrong readback, persistent caching and audio processing.

With the mode environment variable unset, standard 1,600-image and longer
360-image replays exactly match native images, guest records and respectively
4,656,051 / 2,832,756 stereo PCM frames. Runtime initial and selected readback are
both 3. Explicit mode 0 also matches the full standard replay, with initial 3 and
selected 0. Replay uses normal compiled execution, without interpreter checking.
These results do not prove executable bytes never change; intentional mutation
counterexamples remain the accepted semantic limitation.

Both two-minute manual and automatic live/audio launches sustain realtime with
zero added measured underruns/drops. Local desktop/mobile controls, gesture audio,
measured mute/unmute, keyboard/touch, pause/resume and shutdown pass in default 3
and explicit 0. The live routes include the known native-matching level restart;
they do not claim uninterrupted gameplay.

## Preserved optimization experiment

All 24 literal-PC timing observations are committed as 43a244e5f against the
original frozen mode-0 archives. Its remaining diagnostics are held during this
rollout and will explicitly select mode 0. Their controls are the previously
served exact archive, not the new mode-3 default. No new speed measurement is
claimed by this default change; see UNSAFE_CODE_RESULTS.md and
UNSAFE_CODE_ATTRIBUTION_RESULTS.md for all 56 prior measurements and uncertainty.

Actual HTTPS delivery verification follows local acceptance. No Git push.
