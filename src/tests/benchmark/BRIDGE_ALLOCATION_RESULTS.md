# Shared syscall bridge allocation experiment

The bridge closure now captures only its function pointer. Argument positions
are still determined by the same compile-time signature layout, instantiated
inside the closure. SVC dispatch copies the callable, preserving its lifetime
across registration changes, without also copying the unused function name.
Logging still reads the name before calling the snapshot. Locking, guest ABI,
instruction budgets and scheduling remain unchanged. No game identity selects
this behavior.

Real WASM compiler probes show closure sizes for 0/1/4/8-argument signatures
changing from 12/12/36/68 bytes to 4/4/4/4 bytes. These constants demonstrate the
smaller capture; they are not timing evidence. The compiler source is exactly
the accepted V5 baseline, with the unpromoted V6/V7 source changes removed.

Fresh native baseline and candidate builds both pass all three package CTest
targets: ekatests, eka_cpu_tests and bluetooth_netplay. Candidate production WASM
passes 1,152 native call comparisons and 2,880 native memory-fault comparisons.
The complete 179-test compiler suite is explicitly reused: both its frozen JS
and WASM artifacts are byte-identical to accepted V5. Normal control/candidate
and checked stationary Sky Force, normal/checked moving-and-firing Sky Force,
and both Snakes routes match native images, guest records and PCM exactly.
Checked invocations force callback memory; normal replays and existing unit and
fault matrices independently cover direct memory.

The fixed sixteen-observation serial V5/V8 comparison uses reversed pair orders
on all four routes, identical shared settings and original limits, with physical
GPU/shared audio and capture, sampling and detailed counters disabled. All
samples and passive host readings are retained. Builds and correctness runs
finish before timing. An untouched live-archive comparison and actual sustained
live/audio acceptance would still be required before a promotion claim. The
realtime goal remains open. This candidate is unserved; nothing is pushed.

## Sixteen-observation panel

All preplanned observations and passive host readings are retained.

| Route | First pair | Reversed pair | Mean throughput change | Candidate realtime range |
| --- | ---: | ---: | ---: | ---: |
| sky | -0.23% | +8.09% | +3.70% | 0.505–0.563x |
| combat | +5.48% | +4.44% | +4.95% | 0.569–0.582x |
| standard | +9.48% | +4.13% | +6.82% | 1.796–1.805x |
| long | +1.56% | +1.47% | +1.51% | 1.839–1.867x |

Each route has only two matching pairs. Inspect their directions and raw times;
the mean is not a universal gain estimate. This compares V5 and V8 archives,
not the untouched live archive. It does not establish zero Snakes loss or
sustained realtime Sky Force, and is insufficient for deployment.
