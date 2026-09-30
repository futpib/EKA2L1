# Snake growth: coverage gap remains open

The user reports slowdown as the snake grows. Existing performance results do
not establish throughput for arbitrarily long snakes. The older ten-minute
route's minute1/2/3 screenshots all show score200; it is not a length-controlled
experiment. Its periodic heavy windows also recover afterward, so elapsed time
alone is a poor proxy for length. These historical observations use an older
build/browser and include monitor overhead.

A fresh live route on the currently served policy7 executable visibly turns and
collects a pickup (score200 to205), but does not demonstrate a substantially
longer snake. Seven uninterrupted exploratory intervals stay near realtime and
40 presentations per guest second. This is not evidence against the user's
observation: coverage is insufficient. Paused/menu periods are excluded, and
presentation counts are not verified distinct game frames. The top-down camera
also differs from the standard performance fixture.

The route investigation found and fixed wrong browser softkey scan codes; see
SOFTKEYS_RESULTS.md. Short turns during pause-menu dismissal were unreliable;
waiting one second after resume and holding keys100ms visibly turns. Failed
route attempts remain archived under growth-explore-1 and growth-explore-2.
The successful-control trace, screenshots and1Hz counters are in growth-explore-3.
All are under /home/claude/.scratch/eka-benchmark. They are exploratory paced
observations, not promotion timings or a new exact replay claim.

The emulated clock advances from instruction counts in kernel/src/timing.cpp
and skips idle time to the next event. Thus distinguish host throughput
(guest seconds per host second) from game workload/frame rate per guest second.
A compiler speedup can improve the first without fixing a guest CPU/frame-rate
limit. No clock or game-state modification was made.

Next: obtain a route with demonstrably longer snake, preserve input and camera,
compare active windows and a new round in the same emulator, and collect the
user's optional device/browser/level details. Score alone is not exact length.
The ongoing compiler work remains active while this reproduction is unresolved.
Raw window calculations and provenance: SNAKE_GROWTH_EVIDENCE.json.

A further twenty-lane exploration (`growth-explore-4`) still shows score205 in
inspected gameplay images and ends on a level-start screen. It did not establish
a longer snake. This exploration overlapped compiler correctness jobs; none of
its elapsed/realtime measurements are performance evidence. Input and screenshots
remain archived. The required growth-controlled reproduction is still open.

The fifth exploration captures the actual S60 game's instruction pages clearly
(`growth-explore-5/help1.png` through `help5.png`): green pickups restore energy,
red pickups restore boost, boost can alter speed, and emptying the blue Evolver
bar advances a level. Following power paths increases points. These observations
help route design but do not establish a length mechanic or explain the reported
slowdown. Score remains200 in inspected gameplay. Earlier rapid page captures
show transition animations and are explicitly excluded as instruction evidence.
This run overlapped correctness checks and is not performance evidence.

The sixth route runs without competing owned compiler/profile jobs. It visibly
collects a green pickup (200 to205), then shows breakup and a reset to200 while
approaching a marked path. Input, screenshots and counters remain in
`growth-explore-6`. This again fails to establish sustained growth. Rapid opposite
turns may have defeated earlier lane sweeps, but that interpretation is not a
verified input bug. Pauses, screenshots and pacing make this an exploratory route,
not an unpaced throughput result or proof against the reported slowdown.
