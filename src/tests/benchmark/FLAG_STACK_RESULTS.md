# Direct flag-stack stores

**Follow-up:** [post-reboot combined controls](POST_REBOOT_RESULTS.md) are mixed and do not confirm a current combined speedup. The measurements below describe the earlier environment.

The ARM AOT translator now writes computed flags directly from the WASM operand
stack into cached guest-state locals when available. This avoids an intermediate
temporary-local set/get for N/Z and arithmetic C/V flag results. If a state field
is not locally cached, the existing state-memory store remains the fallback.
Guest flag values, instruction budgets, memory guards, validation and fault
behavior are unchanged.

## Correctness and live acceptance

- 134 WASM tests and all three native CTest targets pass.
- Native/WASM fault comparison passes all 480 cases, including callback state,
  callback ordering and exact modified memory.
- The checked native replay matches all 1,600 images, guest records and
  4,919,249 stereo PCM frames through 102.484363 guest seconds.
- Two independent two-minute live runs sustain approximately 1.000x realtime.
  The manual run advances 120.529 guest seconds in 120.546 host seconds; the
  automatic run advances 120.600 guest seconds in 120.582 host seconds. Both
  report zero additional gameplay audio underruns or drops, and pass controls,
  display and shutdown checks.
- Seven frontend smoke checks pass with installed Chromium 153. The separate
  upload/install end-to-end test could not run to completion: its CID fetches
  each returned a 188-byte error body instead of the ROM/RPKG/SIS assets, and
  device installation then failed. This is an input-fetch/test-environment
  failure, not a successful E2E result.

## Real Snakes timings

Serial A/B/B/A batches cover the same heavy 78–96 guest-second window, with
rendering and shared audio processing enabled, capture/profiling disabled and
one browser at a time. Every run executed 3,975,618,624 guest instructions and
676 presentations.

| Batch | Baseline (s) | Candidate (s) | Mean throughput change |
|---|---:|---:|---:|
| A | 14.8717 / 19.0003 | 14.3553 / 13.6994 | +20.7% |
| B, reversed | 15.0854 / 15.1878 | 13.7194 / 13.6095 | +10.8% |
| C | 13.6138 / 15.6583 | 13.8154 / 14.5064 | +3.4% |
| D, reversed | 13.5787 / 14.9038 | 13.6077 / 13.8137 | +3.9% |

Across all 16 runs, mean elapsed time is 15.2374 seconds baseline and 13.8909
seconds candidate, a 9.7% throughput increase by the ratio of means. All four
batch means favor the candidate, as do six of eight adjacent pairs. The raw
baseline contains several slow runs (19.00s, 15.66s, 15.19s, 14.90s); shared-host
variation limits precision. Treat 9.7% as this batch's observed result, not a
portable expected gain. The checked work is identical in every run.

## Decision and reproduction

The repeated direction across four balanced batches, exact replay/fault parity,
and live audio acceptance support retaining the small compiler change. This
does not establish a fixed gain on other hosts or workloads. Raw timing reports,
build hashes, replay comparison and live reports are preserved under
`/home/claude/.scratch/eka-benchmark/flag-stack-timing-{a,b,c,d}`,
`flag-stack-checked1600`, `flag-stack-fault.json`,
`flag-stack-live-manual/report.json` and `flag-stack-live-auto/report.json`.
