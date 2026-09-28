# Browser audio and Qt parity

The former deterministic benchmark was not a live-audio test. Both targets used
`benchmark_dsp_out_stream`, a separate PCM implementation with zero-order-hold
resampling. Interactive WASM discarded its output. Its matching WAV hashes did
not establish parity with Qt's ordinary shared DSP or working browser playback.

## Implementation

Interactive WASM now uses `dsp_output_stream_shared`, the same buffer handling,
stream position, guest volume and buffer-notification logic used by Qt's FFmpeg
DSP backend. PCM16 needs no decoder. PCM8 is converted in the shared path on both
targets; previously the native path advertised PCM8 but sent it into an
uninitialized decoder path. Independent signed-byte fixtures verify conversion.
WASM continues to reject compressed DSP formats; this does not add FFmpeg,
microphone support, or establish MIDI/music-player compatibility.

A guest-thread output device renders every 10 ms using Cubeb's desktop-quality
resampler to 48 kHz stereo. Mono is duplicated, guest/master volume is applied,
and multiple streams mix with signed-16 saturation. Guest time drives buffer
notifications, so the browser device cannot change deterministic execution.
The host queue and AudioWorklet queue each hold at most 0.5 seconds. The worklet
starts after 80 ms of buffering, converts signed PCM to float without changing
pitch, and re-buffers after an underrun. Overflow discards oldest samples and is
counted. A backlog above 200 ms is trimmed to the 80 ms target after startup
catch-up rather than leaving sound permanently behind the display. This bounds latency/memory; it cannot make a stalled emulator produce
missing audio.

The **Enable sound** button creates/resumes the AudioContext from a user gesture;
**Mute** ramps the device gain without altering the guest stream. Muted playback
continues consuming samples. Shutdown closes the context and frees the bridge.
The Web Audio device rate is 48 kHz; unsupported initialization reports an error
and allows retry. Other physical device rates may involve an OS/browser resampler.

The Qt parity mode uses the real native FFmpeg/shared DSP with the same clocked
output device, replacing only its asynchronous Cubeb device callback. Normal Qt
continues using Cubeb. The old benchmark remains available for historical replay;
use `EKA2L1_SHARED_AUDIO=1` for the new audio gate. Recorded metadata distinguishes
these paths. Export pads only the unserviced final fraction of a 10 ms quantum
with silence: it never calls guest code after the final captured instruction.

## Verification

- Two native runs and two browser runs match all **1,600 distinct gameplay
  images**, guest instruction/time records, render events and **4,919,249 stereo
  PCM frames** through **102.484363 guest seconds**. One browser run enables the
  sampled interpreter checker. PCM SHA-256:
  `2bb076a2733ef3a1d9cdc8c3e74d570b1cf649591d9a239260a71ec6a5100d67`.
  Gameplay contains 7,223,662 nonzero scalar samples, peak 8,449.
- Twenty identical native/WASM DSP fixtures cover 8/11.025/22.05/44.1/48 kHz,
  mono/stereo and PCM8/PCM16. They check volume/mute, master gain, callback retry,
  stop/restart, position reset, starvation, deletion, saturation/mixing and queue
  bounds. Their output hashes and callback/position records match exactly.
  An additional known-value PCM8 oracle checks signed conversion independently.
- The production worklet passes exact per-sample stereo, prebuffer, underflow,
  recovery, overflow and reset tests.
- A four-second Snakes segment was played separately through **ordinary native
  Cubeb** and the **actual browser AudioWorklet**, recording an isolated PipeWire
  null-sink monitor as 48 kHz stereo S16LE. Both recordings match all **192,000
  source frames exactly** after removing only leading startup latency: RMS and
  maximum error 0, gain 1. No time warp or gain correction was applied. The
  browser consumes every source frame with zero drops; its single underflow is
  the intentional end of the finite test clip.

This proves the tested PCM path and device transport, not bit-identical analog
speakers, every OS resampler, or an unrestricted normal-Qt realtime run. Fixed
callback scheduling deliberately removes native device-clock nondeterminism.
The new callback schedule changes 10 image hashes, 305 guest timestamps and
all 1,600 instruction records versus the old silent DSP baseline (the final guest
time remains 102.484363 seconds). Fresh native references above are kept
separately. The legacy 85-image/audio comparison still passes unchanged when the
shared-audio option is omitted.

The final 120.565-second live gameplay trial reaches **0.99987x realtime** with
**zero additional underruns or drops** during measurement. Maximum sampled
worklet queue is 7,776 frames (**162 ms**), and maximum sampled gameplay lag is
34 ms. Upload/Start, keyboard/touch, narrow layout, mute/unmute and shutdown pass.

**Cold startup is not gap-free.** With sound enabled immediately, compilation
stalls and catch-up before the measured gameplay window produce six underruns
and discard 152,256 frames (3.17 seconds) through 22 resynchronizations in this
trial. Earlier tests exposed the persistent half-second backlog that motivated
bounded recovery. Enable sound after the game has loaded to avoid those startup
interruptions. We do not claim all-device or all-load glitch-free output.

The deployed HTTPS launcher also passes with a trusted certificate and no
autoplay override: gesture activation, nonzero post-gain waveform, actual zero
output while muted, restored output after unmute, canvas keyboard focus,
keyboard/touch, narrow layout and shutdown. Its post-load audio segment has no
underruns or dropped samples. All 134 existing WASM tests, three native CTest
targets and seven frontend checks pass.

Reload **https://claude-laptop.lan:8188/** and click **Enable sound** once the game
has loaded. Live samples, preceding failed/recovery trials, and final artifact
hashes are recorded in AUDIO_PARITY_EVIDENCE.json.

## Reproduction

```sh
cmake --build build --target eka_audio_probe eka2l1_qt
cmake --build build-wasm --target eka_audio_probe eka2l1_wasm
build/src/tests/eka_audio_probe > /tmp/audio-native.txt
node build-wasm/src/tests/aot/eka_audio_probe.js > /tmp/audio-wasm.txt
diff -u /tmp/audio-native.txt /tmp/audio-wasm.txt
node src/tests/wasm/audio.test.mjs

EKA2L1_SHARED_AUDIO=1 python3 src/tests/benchmark/run_native.py \
  --assets "$ASSETS" --output "$NATIVE" --frames 1600 --repeat 2
EKA2L1_SHARED_AUDIO=1 EKA2L1_BENCHMARK_AOT=5 EKA2L1_AOT_VERIFY=1024 \
  node src/tests/wasm/benchmark.ts "$ASSETS" "$BROWSER" 1600 \
  src/tests/benchmark/snakes.input 21000000
python3 src/tests/benchmark/compare.py "$NATIVE/run-0/frames" "$BROWSER"
EKA2L1_LIVE_AUDIO=1 node src/tests/wasm/live.ts "$ASSETS" "$LIVE" 120
```

For device transport, extract a four-second stereo segment from the shared DSP
WAV. Create an isolated PulseAudio/PipeWire null sink, select it only with the
child process's `PULSE_SINK`, and record its monitor using:

```sh
parec --device=eka_audio_verify.monitor --latency-msec=10 \
  --format=s16le --rate=48000 --channels=2 > capture.raw
PULSE_SINK=eka_audio_verify build/src/tests/eka_audio_probe source.wav
# Then repeat recording separately for the browser:
PULSE_SINK=eka_audio_verify node src/tests/wasm/audio-device.ts source.wav report.json
python3 src/tests/benchmark/compare_audio_device.py source.wav capture.raw
```

Allow the recorder to drain before termination, and unload the test sink after
use. Do not change the user's default audio device. Physical-GPU live runs use
the process-local driver environment described in VALIDITY_AND_COST_RESULTS.md.
