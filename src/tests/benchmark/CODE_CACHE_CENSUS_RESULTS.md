# Recent code-cache misses are a small fraction of lookups

A separate instrumented archive executes the longer route with delivered compiler
policy7/TLB1, guest seconds42-60. Direct counters classify all calls to
validated_code_cache::find(pc, core); diagnostic timings are not promotion data.
No cache decisions or guest state are changed by the counters.

- Lookups: 154,256,142.
- Recent hits: 152,207,817 (98.672%).
- Live-slot conflicts: 2,031,044.
- Empty slots: 17,281; dead slots: 0.
- Dictionary hits/misses: 1,949,603 / 98,722.
- Mapping-generation hits: 154,157,122; refreshes: 298.

The totals reconcile exactly. Guest instructions remain2,987,830,398 with720
presentations, matching the accepted route. This is not a new replay/correctness
acceptance claim. Counts describe the diagnostic run; instrumentation changes
execution cost. No hash experiment or speed claim follows from this census.

Changing the recent-entry hash would add work to every lookup to address roughly
1.3% slot conflicts, so it is not the preferred next optimization. The prior
alignment-aware hash and short-comparison trials also failed to establish gains
(LOOKUP_FOLLOWUP_RESULTS.md). Exact validation remains mandatory. Larger regions
or a bounded direct-call cluster may reduce repeated dispatch overhead, but their
validation, budgets, interrupts and exits must be preserved and measured.

The diagnostic patch and archive provenance are retained. Production source is
restored after capture; LAN remains on the accepted folded data-TLB archive.
