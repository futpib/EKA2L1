# Chrome profiling switch validation

Validated locally on Chromium with NVIDIA Vulkan, stock Nokia 5320 assets,
Snakes standard replay and Sky Force moving/firing replay. The normal WASM
binary SHA-256 is `903d422d2a54e03152cb3803efb9d246dd4f7ca7fd77e743ef5473514b191f46`.
It is identical before and after restoring the build from diagnostic mode.
The baseline is `e0bae151b`, integrating worker history through `2880c2218`.

- Both games produced 33 isolate CPU profiles and complete Chrome traces with
  measurement markers, WASM synchronous compilation and TurboFan compilation
  events. The generated-code samples identified `worker-13` in both captures.
  Traces contain 46,561 events for Snakes and 88,531 for Sky Force, without data
  loss. Guest address labels preserve the original samples and timestamps.
- Whole-run startup tracing completed with 521,931 events and no data loss.
  Its 115 MB JSON includes startup, with CPU sampling confined to gameplay.
  The initial version sampled throughout startup and overflowed; that failed
  capture is retained and explicitly marked incomplete.
- A sampling-off, tracing-off run produced no CPU profiles or trace and was
  labelled `throughput`. Guest-work totals match the corresponding profiled
  window. These validation runs do not establish a speedup.
- Both games matched their native 60-frame reference exactly: every RGBA frame,
  frame record, instruction count, PCM sample and audio event. A further Snakes
  replay with AOT verification stride 4096 also matched exactly on the normal
  build, where custom diagnostics are unavailable.
- Browser capability tests passed with diagnostics both compiled out and
  enabled. Unavailable custom diagnostics return `-2`; disabling them works.
  An actual diagnostic Snakes run collected nonzero scope timers, guest samples
  and 7,334,504 region-exit observations.
- Focused tests passed: 288 guard-publication cases; 270 runner-boundary and
  640 inline-budget comparisons; 16 verifier lookup-protection cases in each
  build; diagnostic ARM/Thumb exit labels and 97 detailed boundary cases.
  Address-label and sample-weighting tests passed, and the legacy summary
  correctly excludes the labelled duplicate profile from its isolate count.

This is focused validation, not a rerun of the entire translation matrix or a
performance promotion. Raw captures, commands, logs and preserved failed runs
are under `/home/claude/.scratch/eka-chrome-profiling`. The compact
[evidence record](CHROME_PROFILING_RESULTS.json) includes reports, hashes and
exact replay results. Use the [workflow](CHROME_PROFILING.md) for new captures.

The Buzz worker acknowledged the explicit stop. Its V32b benchmark process group
was terminated, and its worktree, later commit `e96682fbe`, and partial results
remain preserved outside this baseline. Nothing was deployed or pushed.
