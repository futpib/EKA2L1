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
