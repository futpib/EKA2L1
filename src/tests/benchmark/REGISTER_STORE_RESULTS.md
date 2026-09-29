# Direct register-result stores: rejected experiment

Recovered on 2026-09-29 after the host reboot. The unfinished generic ARM
experiment stored non-flag-setting ALU results directly into cached guest
register locals, avoiding a temporary-local set/get. Flag and PC consumers
retained their original temporary. No budget or memory guard was removed.

The archived candidate matches the recovered build SHA-256. All 134 WASM tests,
672 native/WASM fault cases, three native CTest targets and seven frontend
checks passed after the reboot. Recomparison of the saved checked replay
matched native exactly: 1,600 images, guest records and 4,919,249 stereo PCM
frames. This recomparison uses saved execution artifacts, not a new replay run.

| Batch | Baseline seconds | Candidate seconds | Result |
|---|---|---|---|
| Pre-reboot completed batch | 13.8272 / 14.3109 | 13.7587 / 13.8277 | About 2% throughput gain |
| Post-reboot fresh batch | 13.6150 / 13.5013 | 17.9712 / 16.0682 | About 20.3% throughput regression |

The fresh baseline mean is 13.55815 seconds; the candidate mean is 17.0197.
Both candidate runs are slower than both controls. These are serial ABBA
measurements with physical NVIDIA graphics, shared audio processing, no capture
or profiling, and guest seconds 78–96. Every run executes 3,975,618,624 guest
instructions and 676 presentations. Other workloads were active on the host.
The post-reboot NVIDIA driver is 610.57.04; do not pool these runs with the
previous environment or infer an intrinsic universal regression.

There is no repeatable performance evidence to keep this candidate. Its patch
and binary remain under `/home/claude/.scratch/eka-benchmark/`; the compiler was
restored to the committed flag-store implementation. The incomplete older
`reg-store-timing-a` directory is preserved and excluded from batch statistics.
See REGISTER_STORE_EVIDENCE.json for hashes, raw measurements and checks.
