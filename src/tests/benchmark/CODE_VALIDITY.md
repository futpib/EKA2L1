# Backing-page validity tracking

WASM code-cache entries may reuse an exact byte comparison only when every included code page has an unchanged tracked version. Mapping generation, address-space identity, ARM/Thumb identity, backing and extent checks remain mandatory. Dependencies from inlined helpers participate in the same validation.

Tracked allocations are newly allocated multiple-model code chunks. Initial loader copying uses a private loader accessor, before compiled execution. Guest writes increment versions indexed by physical WASM backing pages, including interpreter/TLB stores, MMU callbacks, direct compiled stores and block transfers. Aliased guest addresses therefore share versions. Existing within-region code-write exits remain in place.

A mutable host-pointer exposure permanently disables version-only validation for its entire allocation before returning the pointer. This covers retained pointers and subsequent writes without requiring callers to announce their lifetimes. Unregistered mappings, escaped allocations, reused tracked backing and version overflow use exact byte comparisons. This conservative fallback is intentional; this is not a comprehensive host-pointer lifetime tracker. IPC string extraction uses bounded copies rather than exposing writable pointers merely to read a string literal.

The tracking table uses 8 MiB of stable storage for the 32-bit WASM address space. Cached stamps cannot dangle after unload. Versions have one writer, the guest CPU; host pointer exposure changes atomic flags. Native builds retain their existing implementation and do not allocate the table.

Validation includes aliases, changed inline dependencies, same-byte writes, host-pointer escapes and retained writes, mapping changes, retirement/reuse, overflow, and actual generated STRB/STRH/STR/STM writes. The exact native-interpreter replay remains the end-to-end correctness reference. Native Dynarmic performance is a separate workload comparison because its budget exits do not exactly match the interpreter.
