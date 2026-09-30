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

Correctness capture jobs overlapped and their wall times are
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

## Reordered longer-route confirmation

Order: proofs, served, protected, protected, served, proofs. Mean seconds:
11.92725 / 12.13945 / 13.64745 respectively. Throughput improves 1.78% against
live, following 2.04% in batch A. All four adjacent live comparisons favor the
combination. The matching policy-0 lead is 14.42%, with visibly slower controls;
it must not be presented as the net live-build gain. All twelve observations
are retained. Standard-scene measurements are next; no deployment.

## Standard-scene holdout: no promotion

All six runs cover the same 78–96 guest-second window, with identical guest
instruction and presentation totals. Mean seconds: protected 14.65455,
proofs 14.44005, live 13.91995. The combination loses both
adjacent comparisons with live, for -3.60% throughput overall.
Closing runs are slower across all variants and remain in the record. The
longer-scene gain does not establish a general improvement; this candidate
remains opt-in and unserved. All eighteen observations are retained.

These measurements precede the requested upstream merge and use immutable
archives. They must not be relabelled as measurements of the merged source.
