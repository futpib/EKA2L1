# Merged-source baseline and CPU profile

Source integration: ef5165ff9, archive merge-20260930-candidate. Two fresh serial,
unsampled, counters-off physical-GPU runs take 11.9434 and 13.6085 seconds for
18 guest seconds (42–60) of the new native-verified longer route: 1.507x and
1.323x realtime. Both execute 3,031,637,220 guest instructions and 380
presentations. Every observation is retained; this is a baseline, not a change
comparison or performance gain over the old archive.

A separate detailed diagnostic counts 183,299,688 compiled dispatches and
3,027,118,557 compiled guest instructions. Detailed scope clocks impose visible
cost: emscripten_get_now alone occupies 12.57% of guest-worker samples. Its
23.944-second elapsed time is not promotion timing and no overhead is subtracted.

A second serial CPU sample with detailed counters disabled gives these worker
shares: generated code and callees inclusive 47.46%, InterpreterMainLoop self
19.16%, validated cache find_original self 13.45%, bytes_match self 10.79%.
Inclusive and self categories must not be added as independent recoverable
costs. This is one diagnostic profile, not a precise causal decomposition.

The next discriminator generates exact expected-byte validators from a snapshot,
comparing constants rather than loading the second byte span repeatedly. Mapping,
address-space and invalidation checks would remain necessary for any eventual
integration. First test pure validation in isolation, including mutations and
memory boundaries; no production guard is replaced and no speed claim is made.

Chrome 153, current system NVIDIA 610.57.04 Vulkan configuration, shared DSP,
original-emitter policy7, folded TLB, grouped scanner2. The old private-library
GPU environment is not used. Raw timing reports, both profile summaries and
hashes of retained CPU profiles are in MERGED_BASELINE_EVIDENCE.json.
