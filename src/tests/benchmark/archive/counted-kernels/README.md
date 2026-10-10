# Historical counted fault matrices

These drivers consume archived native/WASM probes with instruction budgets and
count-valued compiled returns. The matching WASM probe targets were retired when
browser instruction accounting was removed in `0e354cd12`. Their original policy
IDs and result markers are preserved for historical captures; they are not the
current runtime's supported configuration.

Use the archived binaries and fixtures identified by the original experiment
report. Native `cpu_fault_probe.cpp` and the current `test_aot_wasm` interpreter,
callback, memory and watchdog comparisons remain active.

See also the [counted browser-kernel archive](../../../wasm/archive/counted-kernels/README.md).
