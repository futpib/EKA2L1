# Asphalt 3 audio on Qt and WebAssembly

2026-10-12. Asphalt 3: Street Rules 1.2.7 reaches Instant Play and drives with
continuous sound in native Qt and Chromium. This uses the existing recovered
BodyZ container, original N-Gage 1.40.1557 runtime and stock Nokia 5320 ROM/RPKG.
No game or firmware payload was changed. Campaign completion and full-game
activation were not tested; the existing client tile still says TRIAL.

## Fixes and evidence

1. Browser DSP previously accepted only PCM. The game requests MP3, which produced
   repeated format failures. The browser now uses a streaming minimp3 backend
   alongside the existing PCM support and Speex resampler. Partial frames and
   the MP3 reservoir survive guest buffer boundaries; stop/format changes reset
   decoding. The pinned decoder source and its CC0 license are under
   `src/external/minimp3`.
2. A virtual DSP stop leaves the hardware stream open. Its callback previously
   continued consuming samples and requesting guest buffers after the MMF client
   stopped. The shared callback now emits silence while stopped, MMF rejects
   notifications in the idle state, and a new playback clears the previous
   `LastBuffer`. The new stop/restart test failed before this change (two buffer
   requests instead of one after stop). An MP3-only browser candidate also lost
   its guest audio thread with a null-destination `Mem::Copy`; those silent
   timings are excluded. The final candidate passes the same unpaced route.
3. Native FFmpeg queued writes were locked, but decoding read/erased the same
   vector without locking. Decode and compressed low-water checks now use that
   mutex too, avoiding concurrent vector reallocation. Native PCM keeps its
   existing path.

The earlier native launch failure is not attributed to corrupt game binaries:
fresh and old private data prefixes contained identical game executables and
archives, and both subsequently launched. The final native check used the
restored old prefix. The changes above address demonstrated audio defects;
the earlier intermittent installation/startup symptom has no proven cause.

## Actual gameplay and output checks

Host: this workstation's accelerated Xorg/NVIDIA display for Qt and accelerated
Chromium 153 for WASM. Qt uses the normal Dynarmic backend and Cubeb device.
Browser uses the normal paced launcher with instruction totals remaining zero.
An isolated PipeWire sink captures each application's real device output, with
no microphone or other application's audio mixed in.

| # | Check | Result | Scope |
|---|---|---|---|
| 1 | Qt, final build, sound YES | 883 presentations / 30.000 s = 29.43 FPS | Race start, car stationary; later driving reaches 145 km/h. |
| 2 | Qt, continuing drive | 963 presentations / 30.000 s = 32.10 FPS | Ends against barrier at 90 km/h; concurrent browser installation, so a functionality check rather than a controlled benchmark. |
| 3 | Browser, paced, sound YES and AudioWorklet enabled | 917 presentations / 30.382 s = 30.18 FPS | Driving reaches 115 km/h; no new worklet underruns, drops or resynchronizations during the measurement. |
| 4 | Browser, unpaced, sound YES | 150.17 FPS over 15 s | Continuous guest PCM, no captured guest fault/page error; browser output muted in this regression run. |
| 5 | Public LAN launcher, paced, sound YES and AudioWorklet enabled | 371 presentations / 12.151 s = 30.53 FPS | Reaches 107 km/h, then barrier; no new worklet underruns, drops, resynchronizations or captured guest faults. |

The paced browser device recording contains 4,316,516 nonzero signed 16-bit
samples over 44.98 seconds, with signal in every full second. The final Qt device
recording contains 3,421,378 nonzero samples over 41.98 seconds: uninterrupted
signal throughout the active race interval, silence before resume and after the
harness pauses Qt. A second Qt driving recording confirms continuous signal
through the additional 30-second drive. No access violation or guest panic was
captured in the final runs. These are playability/audio checks, not matched
performance comparisons or a complete playthrough.

After deployment, the public launcher was checked again through installation,
sound selection, Instant Play, pause/resume and driving. Its final device capture
(`wasm-lan-driving.pcm`) contains 2,107,230 nonzero samples over 21.97 seconds,
with signal in every full second. The earlier `public-sound-driving` attempt
accidentally left the game paused; its audio assertion correctly failed and that
attempt is excluded. Use `public-verified-driving.json` and `verified.json` for
the completed public gameplay check.

Artifacts are under `/home/claude/.scratch/eka-asphalt3-fix-20261012/`:

- `qt-paced-lock-final/`: native log, screenshots, `verified.json`, `driving.json`.
- `wasm-paced-stop-fix/`: paced browser log, screenshots and `verified.json`.
- `wasm-unpaced-stop-fix/`: unpaced route, log, screenshots and `verified.json`.
- `qt-lock-final.pcm`, `qt-lock-driving.pcm`, `wasm-final.pcm`: actual device
  recordings; adjacent `.json` files contain per-second nonzero sample counts.
- `wasm-lan-final/`: follow-up through the public `.lan:8188` launcher.
- `qt.py`, `wasm-candidate.mjs`, `wasm-paced.mjs`, `wasm-lan-final.mjs` and their
  `steps.log` files preserve the exploratory input/measurement harnesses.

Final Qt SHA-256:
`e958639777e92d96e4d5bf0162fc9090aba065cab1be1784e3c360f867db6fe0`.
Final WASM SHA-256:
`ad02135c672a175de7fb48e17b10df17cfdfc70385278002996599b31ff13284`.
The public LAN service serves the latter from the immutable `served-build/`
directory; fetching `/eka2l1.wasm` verifies that exact hash.

## Reproduction and regression tests

Launch Asphalt 3 through the usual picker. Wait for N-Gage installation, launch
the tile, dismiss its trial screen, choose English, then choose **YES** at the
landscape sound prompt (left softkey/F1). Select Instant Play and dismiss basic
controls. Press Up again after the countdown to accelerate. In the browser,
also select **Enable sound** in the web menu so the AudioWorklet reaches the
output device. Check actual driving images and audio, not just a nonzero FPS.

```sh
cmake --build build --target eka2l1_qt ekatests -j6
cmake --build build-wasm --target eka2l1_wasm -j6
ctest --test-dir build --output-on-failure
cd src/tests/wasm
node --test audio.test.mjs
```

Both builds succeed, CTest passes all three targets, and the browser audio test
passes. New native tests exercise stop/restart notification suppression, whole
versus 1/3/97/512-byte MP3 input fragments, reset, unsupported formats and
22050 Hz mono to 44100 Hz stereo resampling. The test MP3 is synthetic, generated
with `ffmpeg -f lavfi -i sine=frequency=440:sample_rate=22050:duration=0.2 -ac 1
-c:a libmp3lame -b:a 32k -write_xing 0 -id3v2_version 0 stream-tone.mp3`.
No game files or recordings are committed.
