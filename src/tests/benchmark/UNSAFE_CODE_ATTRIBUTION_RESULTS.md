# Unsafe code-mutation cost attribution

This follows the completed fully unsafe / matching-control / untouched-live comparison in UNSAFE_CODE_RESULTS.md. The same frozen archive is used; no compiler, scheduling, source/leaf/site/runner limit or guest-work change is introduced.

Modes: 0 exact control; 1 scan removal only; 2 generated code-write guard and entry-proof code-overlap removal only; 3 both. The archive has version, epoch and write-protection options disabled in every mode. These are deliberately unsafe diagnostics, OFF by default. No deployment or push.

Modes 1 and 2 each pass 40,640 explicitly selected native fault comparisons and exact standard (1,600 images) / longer (360 images) native image, guest-record and PCM replays. Actual-mode readback, correct capture starts and verify_aot=0 are checked. The 175-test suite and mode 0/3 acceptance are reused from the same archive; they are not fresh runs. Intentional stale-code counterexamples remain documented in the full experiment. Replay does not prove no code mutations occur.

The fixed serial plan contains 32 observations: four same-binary modes, both routes, two orders followed by their reversals. Batch A is control/scan/guards/unsafe and reverse; B is guards/unsafe/control/scan and reverse. Thus each mode moves between inner and outer positions. All observations and passive host readings are retained. The untouched live archive was measured in the preceding full-mode comparison, not this attribution panel. Performance counters are disabled during timing; zero counter fields are not mutation or invalidation evidence. Component changes need not be additive because emitted code and browser optimization can interact.

Timing is pending.
