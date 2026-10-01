# Removing earlier emitter peepholes: rejected

This trial keeps the callback-CPSR fix, direct guest loops and precise wide-value
snapshots, but reverses the MOV/MVN unused-Rn change (40ea34499) and flag-stack
store call sites (7cbb39daa). The stack-store helper itself remains because wide
exit snapshots need it. It tests whether their combination hides a better result;
it is not a rollback of the exception fix or a change to guest semantics.

| Batch | Wide-snapshot baseline | Combination | Served |
| --- | ---: | ---: | ---: |
| A | 15.21950s | 15.16535s | 15.93235s |
| B | 14.66545s | 16.08305s | 18.06265s |

Versus the immediate baseline, throughput changes are +0.36% and -8.81%.
The first is nearly tied and the confirmation regresses. Do not retain the
removal. The served comparison is positive in both batches (+5.06%, +12.31%),
but includes a 21.6041s control, far outside earlier fast controls around 13.5s.
That does not isolate a compiler speedup. All timings are retained; no run is
silently discarded or normalized. This motivates a separate own-browser CPU-time
diagnostic before interpreting further small wall-time differences.

A order: wide/combination/served/served/combination/wide.
B order: combination/served/wide/wide/served/combination.
One browser at a time including warmup, physical GPU, shared audio, no capture,
sampling or detailed counters. No owned build/test/profile overlaps timing.
Each run executes 3,975,618,624 instructions and 676 presentations over guest
seconds 78–96. The two symmetric orders do not complete all position rotations;
no third batch was run after the immediate-baseline regression.

All 138 WASM tests, three native targets, seven frontend checks and all six
explicitly rebuilt fault modes pass. Checked replay matches native across 1,600
images, guest records and 4,919,249 stereo PCM frames exactly. The known native-
identical movement-heuristic failure remains. No live/audio acceptance or
deployment follows; the served build is unchanged.

COMBINATION_EVIDENCE.json preserves raw measurements, comparisons and hashes.
The runtime patch is saved as combination_experiment.patch and reverted.
Archive: /home/claude/.scratch/eka-benchmark/combination-candidate, based on
3245ae079 plus its saved patch. WASM SHA-256:
182c869b7aa27c7163673a3c3866fdaee360801211cacba863a5e70225537091.
Reproduce using serial_variants.py with EKA2L1_SHARED_AUDIO=1 and the archived
paths/orders in the evidence. The retained compiler remains the wide-snapshot
implementation; this trial establishes no new LAN speedup.
