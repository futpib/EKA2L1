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
