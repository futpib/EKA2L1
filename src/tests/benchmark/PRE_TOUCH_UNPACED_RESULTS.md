# Five-game unpaced browser measurements

Measured on 2026-10-10 using the current LAN WASM build, Chromium
153.0.8010.52, Intel Core i7-10875H and NVIDIA hardware graphics. The stock
Nokia 5320 device, default compiler configuration and count-free execution
were retained. One emulator ran at a time. Browser audio output was muted;
the normal audio emulation path was retained. Audio performance was not verified.

These are approximate gameplay throughput samples on a shared workstation,
not isolated peak results or a performance regression comparison. Other VM
and compilation activity occurred during parts of the campaign.

| # | Game | Scene | Presentations/s | Guest seconds/wall second | Wall seconds | Emulator compilation ms |
| ---: | --- | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes | Opening level | 34.3 | 4.05x | 30.39 | 43.1 |
| 2 | Sky Force 1.22 | Stage 1, shooting | 247.0 | 7.73x | 5.02 | 6.0 |
| 3 | K-Rally 1.01 | Tutorial track, accelerating and steering | 82.6 | 5.03x | 3.01 | 29.3 |
| 4 | Asphalt: Urban GT 2 1.0.1 | Instant Play motorcycle race | 16.6 | 5.25x | 20.21 | 173.4 |
| 5 | Hooked On 0.74 full (BiNPDA) | Costa Rica, boat movement | 22.3 | 4.24x | 15.11 | 0.0 |

FPS uses the same `eka2l1_presentations` counter as the web UI. It measures
emulator presentations, including rates above monitor refresh; it does not
deduplicate identical images. The guest clock multiplier describes approximate
virtual-time progression. It is not evidence that the entire game advances
that many times faster with equivalent behavior.

Loading and menu navigation precede the accepted timing windows. The reported
compilation totals are the deltas of AOT translation, emission and installation
counters; they do not measure V8's own compilation. Start and end screenshots
were taken outside each window and visually checked. All accepted samples have
zero instruction totals, no recorded fatal/page errors and no route pauses.

The existing test API `eka2l1_route_configure(1800000000)` selects an unpaced
interactive run with a 30-minute virtual deadline. Sky Force, K-Rally and
Hooked On used shorter `route_step_to` deadlines to control startup, followed
by continuous execution to the distant deadline for measurement. Startup pause
and navigation time is excluded. Live launcher settings and emulator code were
not changed.

Short windows for Sky Force and K-Rally are deliberate: longer attempts entered
high scores or tutorial dialogs and were rejected. A second Snakes sample was
also rejected because it captured high scores. The first Hooked On attempt
hit the virtual deadline while idle in the client; it produced no performance
sample and was replaced by controlled startup.

Hooked On used the full 0.74 BiNPDA preservation container tested in
[the installer report](PRE_TOUCH_GAME_INSTALLERS.md). Its boat-map result is
not a fishing-scene measurement. The production LAN picker still selects the
original signed trial. No game binaries were added to Git.

[Machine-readable results](pre-touch-unpaced-results.json) contain exact
metrics, build hash, scene labels and paths to the local raw reports and
screenshots. The harness and captures are under
`/home/claude/.scratch/eka-unpaced-five-20261010/`.
