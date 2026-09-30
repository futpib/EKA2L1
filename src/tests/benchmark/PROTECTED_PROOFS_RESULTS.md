# Invariant spans with watched-code protection

This experiment combines protected writes with the original emitter's delivered
policy 7: invariant read/write span proofs and budget chunks. The preceding
protected policy-0 experiment did not establish a gain over the live archive;
its results remain in CODE_WRITE_PROTECTION_RESULTS.md.

Only policy 7 is newly admitted in the versions build, and only while write
protection is enabled. The frontend rejects disabling protection while that
policy remains selected. Broader IR paths remain disabled. Entry proofs consume
TLB permissions after protection synchronization, so watched destinations cannot
acquire a direct writable proof. Deferred/helper exits retain their existing
precise behavior and per-function proofs are rebuilt after callback returns.

New focused cases check proof selection, a denied watched destination, remapping
the same compiled function to ordinary data, and budgets 0–7 under both linear
and folded indexing. The existing frontend test also checks policy dependencies.

All 163 compiler tests and frontend configuration checks pass. The 32 new
watched-destination/remap/budget cases are included. All 13,760 rebuilt native
fault comparisons pass with policy 7 and protection explicitly verified. Both
policy 7 and the policy-0 control in the same binary match native for 1,600 images,
guest records and 4,919,249 stereo PCM frames. The protected proof policy also
matches the longer route for 360 images, guest records and audio. The generic
fault fixtures are not all watched-page tests; those are covered separately.

Timing is pending. Correctness capture jobs overlapped and their wall times are
not performance evidence. No promotion or deployment is claimed. Raw reports
and archive/source hashes are in PROTECTED_PROOFS_EVIDENCE.json.


## First serial longer-route batch

Same 18 guest seconds, 2,987,830,398 guest instructions and 720 presentations
in every run. Order: protected, proofs, served, served, proofs, protected.
Mean seconds: 13.06240 / 12.67100 / 12.92910. The combination gives +3.09%
throughput against matching protected policy 0 and +2.04% against live. Both
adjacent comparisons favor the combination, although its closing run is only
slightly faster than matching policy 0. All six samples, including the slower
closing runs, remain. Reordered confirmation is required before promotion.
Physical NVIDIA Vulkan rendering and the current 610.57.04 environment are
recorded, with counters and capture disabled throughout timing.
