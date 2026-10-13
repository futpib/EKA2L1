# S60 3rd Edition browser catalogue

On 2026-10-13, the browser catalogue was reduced to eight games using the stock
Nokia 5320 XpressMusic RM-409 firmware 04.13: Snakes, Sky Force, K-Rally,
Asphalt: Urban GT 2, Hooked On: Creatures of the Deep, Asphalt 3: Street Rules,
Tetris and Super Miners.

Explode Arena and Pathway to Glory require earlier Symbian generations. Their
manifest entries and alternate firmware endpoints were removed, together with
per-game firmware selection, the browser card-mount export and launch fallback,
and Pathway's touch-control preset. Original N-Gage card launching is no longer
part of the web frontend. Historical recovery and performance reports remain
historical evidence.

## Verification

The `eka2l1_wasm` Release build completed. Compiler-policy, asset-cache,
installed-image and touch-control tests passed. The touch fixture no longer
expects the retired Pathway preset.

The normal HTTPS launcher at `https://claude-laptop.lan:8188/` exposes exactly
the eight games plus custom uploads. The five former game/card/firmware URLs
return HTTP 404, and the browser runtime no longer exports
`eka2l1_mount_game_card`. Downloaded and frozen runtime WASM hashes match:
`9670e79c417c64f51f129b2b99a907f8a8c112571b4bd98af811d4bab06dc022`.

The real Chromium launcher test exercised switching, keyboard and simultaneous
touch input, mobile layouts, and the custom-file launcher. Reviewed screenshots
show Snakes and Sky Force gameplay. A separate fresh normal launch restored
Hooked On's prepared installation and reached Costa Rica boat gameplay, checking
the simplified prepared-image call signature. No captured page errors or guest
panics occurred.

The complete game-picker integration test failed its two non-silent browser
audio assertions: Chromium reported an AudioContext output-device error, and
the contexts ended suspended with no worklet statistics. This is not a full
audio E2E pass. Audio implementation was not changed. The other five games were
retained without repeating their gameplay or FPS campaigns.

Raw build, browser, catalogue and screenshot evidence is outside Git under
`/home/claude/.scratch/eka-s60v3-only-20261013/`.
