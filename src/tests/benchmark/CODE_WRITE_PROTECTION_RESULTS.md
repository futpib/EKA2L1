# Watched-code write protection

The opt-in implementation removes generated per-store version checks by denying
direct writable TLB tags for watched physical code pages. Existing callbacks
retain version notifications. Mapping, alias, within-region code-write exits and
precise budgets remain. See CODE_WRITE_PROTECTION_DESIGN.md for the mechanism.

Current correctness evidence:

- All 163 compiler tests pass in the versions/lifecycle configuration. Research
  IR and invariant-span paths incompatible with that build remain disabled.
- Focused protection tests pass with both linear and folded indexing, including
  aliases, refills, remaps, partial host spans, deferred stores, precise halfword
  callbacks, pointer escapes and generation exhaustion.
- Both explicit protection modes pass 13,760 native fault comparisons each,
  27,520 total. Mode markers are checked; a deliberately wrong marker is rejected.
  These are general fault tests, not 27,520 watched-code-page tests.
- Both modes match native for all 1,600 standard images, guest records and
  4,919,249 stereo PCM frames, with sampled interpreter checking enabled.
- Native tests and frontend capability/initialization checks pass.

An earlier folded-cache test demanded invariant proofs that code versions
deliberately disable. It now asserts their absence in that configuration while
retaining execution and budget comparisons. Initial replay attempts used an
unsuitable graphics environment, omitted an archive audio script, or selected the
legacy audio backend instead of the reference shared DSP backend. Those attempts
are retained separately. The legacy-mode pair matches itself exactly; it is not
native-reference acceptance. The properly configured runs above are exact.

The protected longer route also matches native for all 360 images, guest records
and audio. Raw evidence and failed setup attempts are retained in
CODE_WRITE_PROTECTION_EVIDENCE.json. Gameplay measurements are pending; no performance
gain, promotion or deployment is claimed. The live grouped-scanner build is unchanged.
