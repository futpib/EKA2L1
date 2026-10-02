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
