# Verified audio replay — 2026-09-26

Two fresh native runs and two fresh Chromium/WASM runs agree exactly on all
1,000 gameplay images, presentation ordinals, guest timestamps, instruction
counts, complete PCM output and timestamped audio events. `compare.py` passes
against native run 0 for each other run; `cmp` also passes for both audio files.
Browser runs completed without observed page errors, failed requests, HTTP errors
or runtime aborts. See [README.md](README.md) for reproduction commands.

| Measurement | Result |
| --- | --- |
| First / last image guest time | 21.026182 / 68.040042 seconds |
| Captured gameplay span | 47.013860 seconds |
| Distinct full images / 3D viewports | 1,000 / 1,000 |
| Changing images per guest second | 21.249053 |
| Minimum adjacent viewport change | 22.0533% |
| Largest image interval | 50.166 ms |
| Final guest instruction count | 10,342,580,529 |
| Audio output | PCM16 LE, 48 kHz, stereo |
| Audio sample frames, from guest time zero | 3,265,922 |
| Guest stream configuration in this replay | PCM16, 8 kHz, mono, volume 5/10 |
| Guest buffer writes / requests | 1,392 / 1,391 |
| Nonzero gameplay channel samples | 4,417,622 |
| Gameplay sample peak | 4,121 / 32,768 |

The WAV includes startup and ends at the final image's guest time, rounded down
by less than one sample interval. The accompanying local video trims audio to
the first gameplay image and uses the frame timestamps; its AAC track is a
playback encoding, while exact comparisons use the original PCM.

## Exact source and artifact state

- Baseline implementation: `dd3babbd4488ee6ddebdea37ab204e776363250f`.
- Audio runtime: `a9ab0165b4bfcd8040ddf8672e6b998ef9203136`. All four full-run
  reports record this HEAD and a clean worktree. The native and WASM binaries
  contain the same runtime sources as this commit.
- Runner validation: `a73c9a3cd1351ce3f62f9f2d411e9e88e17fa496`. This changes only
  runner validation/documentation, with no emulator changes. A fresh 20-image
  browser run at this HEAD also passes and matches the native smoke capture.
- Full native CTest at the runner commit: CPU target passes; `ekatests` has
  **76 passing and seven failing cases** (395/402 assertions pass). Both new
  audio cases pass. The seven failing assertion sites exactly match the saved
  pre-audio log: three allocator, three number-parsing, one app-registration.
  No clean upstream-base run was performed, and the full native suite is not green.
- WASM CPU/AOT suite at the runner commit: **121 passed, zero failed**. The game
  benchmark itself uses the interpreter, not experimental AOT.

SHA-256 values:

```text
native executable  271bd2bbee1fb824445c8e6c030b811f7a2deb195b1c1f48c4505563d66a5d4c
WASM binary        a8ef7768043b4d8017cda706b3bbd99f34481a67fcb9c4628cbfcd99d2e29c3f
PCM sample bytes   459268607f7e5b52e840e2c2e43bba61b6e8f3c0dd10ebb0291cc5a45e982cff
audio event log    24ccd1b4273371f00ca8a1b145c4b4cb41d6a18547b55887a46399ebcd1a82c8
```

Local evidence is under `/home/claude/.scratch/eka-benchmark/`: native captures
in `audio-native-{0,1}/run-0/frames`, browser captures in `audio-wasm-{0,1}`.
Comparison and gameplay summaries are in `/home/claude/WORK_LOGS/` as
`EKA2L1_AUDIO_VERIFICATION.json` and `EKA2L1_AUDIO_GAMEPLAY_VALIDATION.json`.

This verifies one fixed game/device/input replay. Audio is rendered on the guest
clock and exported for playback afterwards; real-time speaker playback is not
added to either frontend. It does not establish hardware timing or DSP fidelity,
compressed-codec support, microphone input, or general deterministic multimedia.
