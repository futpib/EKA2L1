# CMP operand reuse

The rejected experiment uses the operands left by an immediately preceding
unconditional CMP for the following compound signed/unsigned condition (HI, LS,
GE, LT, GT, LE). It still materializes every architectural flag. Joins,
intervening instructions, conditional compares and helper boundaries invalidate
reuse. No guest address or game name selects it.

## Measurement and decision

Shared audio processing and physical GPU enabled; capture, profiling and detailed
counters disabled. All twelve runs, including warmup, are serial. No owned
build/test/browser job overlaps timing. Each measures guest seconds 78–96,
3,975,618,624 instructions and 676 presentations. All outliers are retained.

| Batch | Direct-loop baseline | CMP candidate | Served archive |
| --- | ---: | ---: | ---: |
| A | 14.26215s | 13.46610s | 14.39940s |
| B | 14.22920s | 14.37400s | 13.53035s |

A order: direct/CMP/served/served/CMP/direct.
B order: served/CMP/direct/direct/CMP/served.

Throughput changes versus the immediate baseline are +5.91%
and -1.01%; versus served they are +6.93%
and -5.87%. Slow controls inflate the first batch; its closing
direct control is 14.9265s and one served control is 15.3540s. The reversed batch
loses against both controls. This does not establish a repeatable speedup.
The runtime change is removed; independent regression tests remain committed
as da1829d58. No deployment or new live/audio acceptance is claimed.

## Correctness and reproduction

All 137 WASM tests pass, including 64,512 new exact compare/condition cases,
464,912 bounded execution cases and retained interrupt/ALU/multiply coverage.
All three native targets and seven frontend checks pass. Explicitly rebuilt
native/WASM fault probes match in all four 672-case modes. The checked replay
matches native pixels, guest records and 4,919,249 stereo PCM frames across
1,600 unique images. The native-identical movement heuristic failure remains
unchanged (408,561 us maximum interval, 0.002483 minimum viewport change).

The candidate was built at 18b450f73 plus its archived source/test patch; its
WASM SHA-256 is 8e0ffc160b1ade95cb9075280e2c63b7618392cc6cbe1d8550b5502a58f22648.
Its independent tests were subsequently committed without changing runtime code.
The runtime-only compare_condition_experiment.patch applies to da1829d58.
Use serial_variants.py with EKA2L1_SHARED_AUDIO=1 and the orders above; archive
paths, exact raw measurements, hashes, logs and comparison outputs are in
COMPARE_CONDITION_EVIDENCE.json. Current source retains the direct-loop change;
LAN continues serving addv-candidate.
