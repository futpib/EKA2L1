# Snakes service overlap, 2026-10-01

No substantial new unawaited OS/library computation was identified in the observed
gameplay. Audio has demonstrable overlap, but its measured processing cost is
small. Graphics already has a worker. The proposed EZLib decompression target
executes during startup, not during this gameplay.

The evidence is in [evidence.json](evidence.json). Raw traces, per-isolate browser
profiles, screenshots, build manifests and source snapshots are retained under
`/home/claude/.scratch/eka-benchmark/service-overlap-*`.

## What actually ran

Two diagnostic replays and one disabled-census control used the same underlying
build inputs, compiler policy 7, shared audio and hardware NVIDIA Vulkan/ANGLE.
All reached 96 guest seconds with 16,200,139,186 executed guest instructions.
The measured 78–96 second window contained 4,242,626,167 instructions and 380
presentations. These are this replay's counts; do not substitute the counts of
earlier optimization experiments with other source/input snapshots.

| # | Operation | Observed gameplay | Dependency / overlap | Implication |
| --- | --- | --- | --- | --- |
| 1 | EZLib ordinal 32, `CEZDecompressor::DecompressL` | Five entry/return pairs before 21 seconds; none from 21–96 seconds | Synchronous whole-buffer call | A possible startup optimization, with no measured steady gameplay opportunity. |
| 2 | File reads | None from 21–78 seconds; four reads totalling 4,096 bytes from 78–96 seconds | All four used synchronous IPC and completed inline | No substantial unawaited I/O here. |
| 3 | File/feature/font server IPC | 1,240 calls in 78–96 seconds; all issued synchronously | Completed inline where a request status was supplied; no new asynchronous IPC calls in this window | Small synchronous services, not a queue of background jobs. |
| 4 | Audio buffer-ready notification | 375 registrations in 78–96 seconds; 374 requests issued there completed before the endpoint | All 374 remained pending across guest execution; 244 spanned at least one presentation | Real overlap exists. The notification interval is not the audio computation cost. |
| 5 | Audio processing | 1,800 ten-millisecond virtual audio pumps in the 18-second window | Same shared audio backend; processing still occurs on the emulator thread | Inclusive instrumented wall time was 121/129 ms in diagnostic runs and 94 ms in the control. No large offloading benefit is established. |
| 6 | Window event subscriptions | Input/event requests can remain outstanding across game execution | They wait for events, rather than perform expensive computation | Long pending time does not identify a useful compute job. |
| 7 | Graphics | Screen update and blit calls throughout gameplay; existing graphics worker | Replay adds an artificial completion wait; the ordinary live path overlaps rendering | Do not propose moving graphics to a worker as a new optimization, or misread replay waits as live behavior. |

The completed heavy-window audio requests had a median lifetime of 43,229 guest
microseconds and a median 8,105,785 issuing-thread instructions before completion.
370 of 374 also encountered a blocking `WaitForAnyRequest` while outstanding;
that wait can concern another request. The game demonstrably resumes useful work
before audio completion, so “ever waited” would incorrectly reject this overlap.

The other observed guest threads were AknIconSrv, ECom, CdlServer and BackupServer.
They executed only before 21 seconds. After 21 seconds, **all observed guest
instructions belonged to Snakes' main thread**. Guest libraries called by that
thread are included. Moving a synchronous call to another worker and blocking
this caller would not uncover another currently running guest thread to execute.

## Ordinary interactive path

The existing live harness was also run with the private diagnostic build, the
real AudioWorklet, keyboard/touch inputs, and the ordinary graphics presentation
path. Chromium retained its process-level mute to avoid audible shared-host
output; application audio rendering and worklet consumption remained active.

Its measured live window advanced from 23.376721 to 58.516486 guest seconds over
35.130816 host seconds, with 744 presentations. The harness passed UI/input,
changing gameplay, audio and shutdown checks. There were zero added audio
underruns and zero added drops during that window.

The complete live census ended at 58.716415 guest seconds. It again found five
startup decompressions, no gameplay decompression or file reads, and only the
game's main guest thread executing after 21 seconds. In the 21-second-onward
census there were 785 completed audio notification requests, each spanning guest
execution, with a median 49,970 guest microseconds and 7,674,128 issuing-thread
instructions. The live path does not enable the replay presentation counter;
per-request frame counts therefore come from the replay, not this live run.

## Validation and limitations

The second diagnostic has 7,566 tracked requests: 7,539 completed and 27 still
outstanding at the endpoint, with zero replaced or dropped records. The pending
set includes long-lived notifications and the last audio request. Thread totals
and per-phase instruction totals agree with the emulator endpoint. The live
census also has zero replaced/dropped records and reconciled accounting.

Both diagnostic replays and the disabled-census control matched instruction
endpoints, presentation counts and final RGBA pixels exactly. Their final image
SHA-256 is `2bc0a1a3c56a2660512d190cac9f14ddd45191d21e16ef3fd33b4c3de64ea5cf`.
No-readback mode does not export PCM; no full image-sequence or PCM equivalence is
claimed. Screenshots were inspected and show gameplay.

These are instrumentation runs on a shared host with concurrent unrelated
experiments. Durations are inclusive wall time and are not isolated CPU costs or
optimization speedup trials. Detailed counters and sampling add overhead. Guest
instruction progress is resolved at dispatch boundaries, within 4,840 instructions;
notification lifetime must not be treated as compute time. The first exploratory
trace missed some guest-server completions; the second adds those hooks and is
the authoritative lifecycle census.

No offloading or library replacement was implemented. The measured next-step
decision is to avoid a broad OS-worker rewrite for this replay. Audio is a valid
overlap boundary with little measured cost; decompression is a startup-only
candidate. A different game, scene, streaming workload or codec could differ.
