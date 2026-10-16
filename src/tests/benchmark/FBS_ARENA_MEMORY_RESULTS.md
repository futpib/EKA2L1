# FBS backing and process arena memory

Measured 2026-10-16 on `wasm-port`, stock Nokia 5320, Chromium 153, with fresh
browser processes and the persistent-device launch path. Asphalt 3 reached its
race HUD with sound enabled; Tetris reached its marathon board. Both had no
guest errors. The control was the deployed `8bacf4e12` WASM build. The final
candidate was `dba61b5a7f1a470f3b7bcc1293ef1d0ed79b0b870f54fe5357fb5629111b714e`.
The FBS control changed only its backing allocation in that candidate and was
`c34b2d082b853b5810a0294cbb76def483c8d1451db854b6105fc2660f9ba48c`.

| # | Workload and build | Arena reserved, MiB | Arena initialized, MiB | WASM capacity, MiB | Resident WASM mapping, MiB | Browser PSS, MiB |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Asphalt 3, deployed control | 832 | 192 | 1708.81 | 837.75 | 2859.88 |
| 2 | Asphalt 3, 48 MiB arena and fresh FBS backing | 624 | 160 | 1473.00 | 830.21 | 2826.66 |
| 3 | Asphalt 3, same candidate with ordinary FBS backing | 624 | 160 | 1471.31 | 864.43 | 2873.28 |
| 4 | Tetris, deployed control | 832 | 224 | 1690.38 | 895 | 2732.38 |
| 5 | Tetris, 48 MiB arena and fresh FBS backing | 624 | 176 | 1402.13 | 841 | 2923.06 |

FBS's 96 MiB large chunk previously had 96 MiB resident despite only 2.316 MiB
of guest commitment in the [allocation profile](ASPHALT3_SPARSE_MEMORY_PROFILE.md).
The new FBS backing uses untouched, page-aligned WASM heap pages and zeros each
page before publishing a guest commit. A small pool reuses that backing across
sequential emulator sessions. In the isolated Asphalt 3 comparison, it reduced
resident WASM memory by **34.22 MiB** and whole-browser PSS by **46.62 MiB**.
WASM capacity rose 1.69 MiB, so this is a residency improvement, not a capacity
improvement. The ordinary allocator remains the fallback if fresh backing fails.

Process arenas keep stable host pointers and grow their published, initialized
prefix on guest commit. Creating an 8 MiB chunk no longer initializes all 8 MiB.
The 48 MiB reservation still contains every arena-backed chunk in the
[nine-app access trace](MEMORY_ACCESS_DISTRIBUTION_RESULTS.md), including the
46 MiB installer extent; larger or different allocations use the existing page
table. In these 13-process scenes, arena reservations fell **208 MiB**.
Asphalt 3's WASM high-water capacity fell **235.81 MiB** and Tetris's fell
**288.25 MiB**. Their initialized prefixes fell 32 and 48 MiB respectively.

The whole-browser PSS result is workload-sensitive. Tetris's resident WASM
mapping fell 54 MiB, yet its total browser PSS rose 190.68 MiB in these
captures. Its control and candidate advanced different numbers of frames and
compiled functions during the same wall-time window, so total PSS and FPS are
not clean fixed-work comparisons. Asphalt 3's direct-page count was unchanged
(2,003), and the nine-app trace supports unchanged direct-path coverage. CPU
frequency and unrelated host load also varied substantially, so these runs do
not establish a runtime speedup or slowdown. An early FBS prewarm variant was
rejected after increasing both WASM capacity and browser PSS.

The final candidate passed 197/197 WASM compiler tests, 915 native CPU
assertions, and real browser Sky Force-to-Snakes switching with touch, keyboard,
audio and mobile layout. Raw captures are in
`/home/claude/.scratch/eka-fbs-arena-{baseline,candidate,prewarm,48m,48m-no-fbs,tetris-baseline,tetris-48m}-20261016/`.
