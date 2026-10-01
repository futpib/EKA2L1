# Runtime service overlap census

This diagnostic follows the real browser Snakes replay. It measures IPC issue,
completion and blocking-request waits; audio buffer notifications; synchronous
host handlers; and entry/return of the ROM's EZLib ordinal 32. It does not change
guest scheduling or introduce background jobs.

`build.py` snapshots the existing WASM link inputs and required headers, recompiles
eight translation units with diagnostic hooks, and links in a private output
directory. It does not rebuild or overwrite the live build. The manifest records
the base archive hashes, original and instrumented source hashes, compiler/link
commands and resulting WASM hash. The original source files are retained next to
their modified copies. This is an overlay on the captured build, not a claim that
the running build was reproduced from the current Git commit alone.

```sh
python src/tests/benchmark/service_overlap/build.py PRIVATE_BUILD
python src/tests/benchmark/service_overlap/run.py PRIVATE_BUILD ASSETS NEW_RUN
python src/tests/benchmark/service_overlap/summarize.py NEW_RUN
python src/tests/benchmark/service_overlap/run.py PRIVATE_BUILD ASSETS CONTROL_RUN \
  --enabled 0 --sampling 0
python src/tests/benchmark/service_overlap/run_live.py PRIVATE_BUILD ASSETS NEW_RUN LIVE_RUN
```

The runner uses the repository's real browser replay harness, a hardware GPU,
shared audio, compiler policy 7, and rendering without frame readback. The default
profile window is guest seconds 78–96. Lifecycle events are collected from startup
and grouped by issue time: before 21 seconds, 21–78 seconds, and 78–96 seconds.
The decompressor address is read from the supplied ROM export table. A sampled
CPU profile is recorded per isolate; parked workers are not counted as useful CPU
execution.

For each tracked request, the census records whether it completed before the
issuing call returned, guest instructions executed before completion, the first
blocking `WaitForAnyRequest` while it was outstanding, and presentations during
the request lifetime. The latter counts presentations only inside the selected
profile window. Both ordinary IPC completion and retained `notify_info`
completion are observed. Failed sends and inline status completion are recorded.

Interpretation limits:

- `WaitForAnyRequest` can be waiting for a different outstanding request. A
  recorded wait is not proof that the game awaited this particular operation.
- Conversely, executing instructions is not by itself proof of useful overlap;
  presentation progress supplies additional evidence during the profile window.
- Instruction progress is accounted at guest dispatch boundaries, with up to
  4,840 instructions of boundary uncertainty. Do not read a small or zero count
  as an exact instruction-level dependency trace.
- Notification lifetime is not computation time. Input subscriptions and audio
  buffer-ready notifications are not jobs consuming CPU throughout that interval.
- Handler and decompressor durations are inclusive wall times from an
  instrumented run. They can include preemption, nested work and synchronization.
  Do not add overlapping scopes or use these runs as optimization speedup trials.
- Decompression entry observation covers the named ROM API through the current
  region dispatcher. It does not identify every possible game-internal codec.
- Pending requests at the endpoint are right-censored. Check dropped/replaced
  records before interpreting lifecycle coverage.

Compare guest instruction endpoints, presentation counts and the final rendered
image against the disabled-census control. This no-readback profile mode does not
export PCM or audio event artifacts. It does not replace a full frame-by-frame
and PCM correctness comparison for a future optimization.

The replay frontend forces graphics completion even without readback. Therefore
its graphics wait duration must not be described as the ordinary interactive
pipeline's wait. `run_live.py` additionally runs the existing interactive harness,
including keyboard, touch, actual AudioWorklet consumption and shutdown. It keeps
Chromium's process-level audio mute to avoid audible output on the shared host.
Its request census is read before shutdown; no phase-2 presentation counter is
enabled in that path, so per-request presentation counts are unavailable there.
Interactive inputs and endpoints are not identical to the deterministic replay.
