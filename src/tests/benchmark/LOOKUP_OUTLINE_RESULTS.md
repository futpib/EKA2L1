# Outlined code-cache lookup: acceptance

The opt-in experiment moves container recovery and mapping refresh out of the
frequent recent-entry path. Full key/live checks, mapping-source and generation
identity, exact primary/dependency bytes and terminal rejection remain. See
LOOKUP_OUTLINE_DESIGN.md for the design and static-layout limits. Code-version
and lifecycle alternatives remain off. The LAN build is unchanged.

All 162 WASM compiler tests pass. The new focused cache regression exercises both
lookup layouts, including rejection without resurrection, host writes, remaps,
dependencies, collisions, address spaces and legacy generations. All 32 native
CPU tests pass (531 assertions), with existing cache regressions expanded to both
layouts. Frontend controls reject invalid and post-initialization selection.

Both rebuilt native fault matrices match exactly: 13,760 cases per explicitly
selected lookup mode, 27,520 total. Every result verifies lookup, scanner, TLB and
compiler policy markers. A deliberately wrong lookup marker request is rejected.
Both layouts match native for all 1,600 standard images, guest records and
4,919,249 stereo PCM frames. The outlined layout also matches the longer route's
360 images, guest records and audio. These passes cover the recorded cases;
they are not a proof of all emulator behavior.

Implementation: 2fb326960. Archive: lookup-outline-candidate. Its binary and source
hashes are rechecked before this report. Logs, markers, exact comparisons and
function-size evidence are in LOOKUP_OUTLINE_EVIDENCE.json. No performance result
or deployment is claimed. Next: serial original/outlined modes in one binary,
with the exact delivered grouped-scanner archive as an external control.
