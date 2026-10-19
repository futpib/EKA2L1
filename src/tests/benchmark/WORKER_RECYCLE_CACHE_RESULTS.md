# Bounded ROM module cache across CPU Worker recycling

The cache experiment retains generated ROM WASM **bytes** in an OPFS file while
the browser terminates CPU Workers to reclaim their V8 code. A replacement
Worker loads a cached module only when execution requests one of its guest
addresses. The page keeps no `WebAssembly.Module` or `WebAssembly.Instance`
references for this cache. The experiment is opt-in with `?recycleCache=1`;
normal launches keep the existing one-time installer recycle policy.

The active file is capped at 4 MiB by default. Each record has its length and
XXH64 digest; a failed read or instantiation falls back to translation. Records
belong to one page session and ROM mapping. At each Worker retirement after
the first, modules not loaded in the retiring Worker generation are omitted
from a compacted file. The old and new files can briefly coexist during
compaction, so transient OPFS storage can approach 8 MiB. A normal emulator
shutdown deletes the file; reload also removes the previous session's file.
Orphaned files from closed tabs are removed after seven days on a subsequent
experiment launch.

All runs below used Chromium 154, the stock Nokia 5320 firmware, the full
Hooked On 0.74 package, a fresh device install, and the scripted path through
the dismissed tutorial to a stationary boat scene. The renderer was in its
own 1,843 MiB cgroup with zero swap. A passing run reached the boat scene and
three gameplay windows, with `oom=0` and `oom_kill=0`. The peak is the
renderer cgroup's `memory.peak`, **not** whole-browser PSS or the WASM heap.
The runner is `src/tests/wasm/renderer-memory-limit.mjs` with
`EKA2L1_BENCHMARK_FRESH_LAUNCHER=1` and the indicated recycle settings.

| # | Configuration | Run | Peak renderer MiB | Client ready | Boat after tutorial | Gameplay FPS, three 10 s windows | Outcome |
| ---: | --- | --- | ---: | ---: | ---: | --- | --- |
| 1 | Stock, one recycle at 50k functions, no cache | R1 | 1681.4 | 72.7 s | 152.7 s | 211 / 272 / 266 | Pass, no OOM |
| 2 | Stock, one recycle at 50k functions, no cache | R2 | 1652.0 | 84.8 s | 181.7 s | 81 / 70 / 81 | Pass, no OOM; heavy unrelated host load |
| 3 | Cache 4 MiB, repeated recycle at 20k | R1 | 1628.7 | 106.9 s | 186.1 s | 195 / 268 / 219 | Pass, no OOM |
| 4 | Cache 4 MiB, repeated recycle at 20k | R2 | 1681.2 | 105.6 s | 183.9 s | 196 / 232 / 288 | Pass, no OOM |
| 5 | Cache 4 MiB, repeated recycle at 10k | R1 | 1626.9 | 138.0 s | 218.1 s | 138 / 137 / 270 | Pass, no OOM |
| 6 | No cache, repeated recycle at 20k | R1 | 1654.4 | 113.1 s | 194.5 s | 191 / 66 / 114 | Pass, no OOM; heavy unrelated host load |
| 7 | Cache 4 MiB, repeated recycle at 20k, final code | R3 | 1670.1 | 117.1 s | 222.4 s | 196 / 205 / 204 | Pass, no OOM; heavy unrelated host load |

The 20k cache runs restarted the CPU Worker eight or nine times during
installation, then continued through gameplay. Their final files held 4.0 MiB
and 581, 507 or 507 modules. They replayed 1,993, 2,768 or 2,495 modules
across Worker generations, representing 27.8, 29.6 or 29.3 MiB read from
OPFS. Collection removed 591 modules / 4.9 MiB in R1, 317 modules / 1.9 MiB
in R2 and 588 modules / 4.2 MiB in R3. All reported zero cache failures.
In R3 the browser's `FS.stat` returned 4,194,241 bytes, exactly matching the
C++ counter and below the 4,194,304-byte cap. These are cumulative
write/read/collection counts, not simultaneous live memory. R3 skipped about
648 MiB of attempted writes after the cap; that counter includes repeated
attempts to store the same module, so it is not a unique-code size. The 10k
setting produced roughly twice as many
Worker restarts and substantially longer startup without a convincing further
peak reduction, so the opt-in experiment uses 20k.

The current evidence proves that the bounded cache, replay and generational
collection work in a real full game route. It **does not prove a net RAM win**
over stock or over the repeated-recycle control: their peaks overlap the 20k
cached peaks (1629–1681 MiB). The clean stock R1 reached the post-tutorial
boat about 31–34 seconds before the first two cache runs. In the final pair
under unrelated host load, no-cache reached the client in 113 seconds and
peaked at 1654 MiB; cache-on reached it in 117 seconds and peaked at 1670 MiB.
The cached route needed extra in-game pause attempts, so its 222-second boat
time is not a clean startup comparison with the no-cache route's 195 seconds.
An unrelated Android emulator and Gradle build consumed several CPU cores
during stock R2 and this final pair, so FPS is not a reliable cache effect.
The cache remains opt-in; current evidence does not justify promotion.

The served `.lan` release uses WASM SHA-256
`94b7d0bb57f9915518a4d6a0e4f3507e8760f6f001fe8d5d3fb01cd59725506d`.
A Chromium route reached Snakes gameplay with the cache inactive, and a fresh
Hooked On launch with `?recycleCache=1` reported the active 4 MiB/20k setting.
The full Hooked On fixed-cap runs above preceded a final HTML gate that only
skips cache setup when there is no N-Gage package to install.

Raw routes and renderer cgroup records are in
`/home/claude/.scratch/eka-recycle-cache-{stock50-off-r1,stock50-off-r2,nosplit-on-r1,nosplit-on-r2,nosplit-10k-r1,repeat-nocache-r2,final-on-r1}/`.
