# S60 softkey mapping correction

The live browser launcher sent Application0/1 (180/181) for its left/right
softkey buttons and F1/F2/Escape keys. In Snakes these did not open the pause
menu. Raw Device0/1 (164/165) correctly pause and resume the game. The launcher
now uses those codes for keyboard and touch.

The candidate archive is /home/claude/.scratch/eka-benchmark/softkeys-candidate.
Only generated HTML differs from deferred-counts-candidate; JavaScript, WASM,
data and audio files are identical. Compiler policy7 remains selected. WASM
SHA-256: dcceea4c30d544b55af11b79d403cabb342b2e5474c7afa41a84ecd6b216464b.
This is an input correction, not a performance optimization.

## Verification

The local browser run verifies a visibly open pause menu after F1 and Escape,
resume after F2, touch-left resume and touch-right pause, then Escape resume.
It also passes gesture sound, measured mute/unmute, directional keyboard/touch,
mobile overflow, visible gameplay and shutdown without page/request errors.
The short sound sample has no underruns or drops. This is not a new two-minute
live-audio acceptance claim.

The compatible frontend suite from026ffc82d passes all nine checks. The current
suite expects additional research IR capabilities absent from the unchanged
served WASM and fails that capability assertion; its log is retained. A first
OCR check missed visibly present small text; the retained screenshot confirms
the menu. The corrected check crops/enlarges the caption before recognition.

No full compiler/replay rerun was needed for this HTML-only edit. The served
application's previous exact image/audio and fault evidence applies to its
unchanged executable, not to hypothetical future builds.

Actual HTTPS delivery passes the same six pause/resume checks, gesture audio,
mute/unmute, layout and shutdown with no page/request errors. Downloaded JS/WASM
hashes match the unchanged tested archive. Reproduce with EKA2L1_WASM_BUILD_DIR
pointing at the archive, then node src/tests/wasm/softkeys.ts ASSETS NEW_OUTPUT
[EXISTING_URL]. ImageMagick and Tesseract recognize the actual Snakes pause
caption; screenshots and all raw observations remain in SOFTKEYS_EVIDENCE.json.

The snake-growth investigation remains open. This fix makes its controlled
route exploration possible; it does not establish the cause of reported slowdown.
