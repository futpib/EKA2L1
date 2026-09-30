# Guest-time route stepping for growth investigation

The optional route control parks the guest CPU between system-loop iterations
until a later guest-time target is supplied. Input uses the normal live queue;
guest time, registers, game memory and assets are not rewritten. Graphics may
finish pending presentation. Targets are approximate: a system-loop iteration
can pass the requested threshold; the controller records actual guest time.

The control is disabled by default, configured before initialization, and has no
launcher UI. Stepped execution skips host pacing, so its elapsed times, audio
underruns and presentation timing are not performance evidence. Its purpose is
route exploration without the game's pause-menu transition. Any useful growth
route must be replayed through ordinary execution for slowdown measurements.

src/tests/wasm/route.ts verifies paused guest time stability, rejects invalid and
late configuration and non-increasing deadlines, checks input consumption, and
shuts down while parked. The first complete session passes those checks with no
browser errors. It reproduces startup at guest time 23,000,000 us and score 200,
collects a pickup to reach 205, then collides. That does not establish sustained
length growth. All inputs, actual step times and screenshots remain archived.

The disabled control's interpreter-checked replay matches all 1,600 native images,
guest records and 4,919,249 stereo PCM frames. All nine existing frontend checks
pass on this archive. The compiler itself is unchanged; prior compiler and fault
artifacts have identical hashes and were not relabelled as fresh runs.

Archive: /home/claude/.scratch/eka-benchmark/route-step-candidate.
WASM SHA256: 0b60d48f92f2e65d0c3011e82e11f63af68ed8784de330fe659e03ff75808eda.
Source base: 8e02c72e1 plus archived patch. Detailed evidence and hashes are in
ROUTE_STEP_EVIDENCE.json. No deployment or push. The longer-snake report remains
unresolved; route exploration continues.

## Ordinary served replay of the second route

The second exploration collects seven energy pickups (score 200 to 235) and
the optional NOKIA N. Shutdown while parked passes without browser errors.
The saved 74 press/release events were replayed with ordinary execution using
the exact served policy-7 archive, without the route gate. The final image
at guest 60.939113 seconds shows score 235 and the same N. This establishes
reproducible progression through that route, not substantial snake growth or
power-path completion. Earlier command names containing "path" were navigation
hypotheses; the destination proved to be the letter room.

Eight successive approximately five-guest-second windows from guest 21 to 61
seconds retain 19.97 to 20.11 distinct captured images per guest second and
39.95 to 40.23 presentations per guest second. Guest instruction rates range
from 161.51 to 171.07 million per guest second. This route shows no sustained
decline in guest frame cadence. Screenshot capture changes host load, and these
are guest-time rates, not realtime throughput measurements. Snake length was
not measured, so this neither establishes nor refutes the reported length
dependence. A longer route with observed length differences remains needed.

Raw route: /home/claude/.scratch/eka-benchmark/growth-step-2.input
(SHA256 8e4183e6a4397f5459fb5c1a3cf6e6dbb959bb3c7b622f0cc3e40da5dbb9a1dd).
Replay: /home/claude/.scratch/eka-benchmark/growth-normal-replay-2.
WASM SHA256 dcceea4c30d544b55af11b79d403cabb342b2e5474c7afa41a84ecd6b216464b.
The replay has 800 distinct images; it is a route reproduction, not a new
native correctness baseline. No deployment or push.
