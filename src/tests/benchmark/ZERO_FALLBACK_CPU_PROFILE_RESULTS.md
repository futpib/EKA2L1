# CPU sampling after eliminating measured interpreter fallback

Frozen V26 archive, normal browser flags, detailed counters disabled. Three profiles collected serially after the eight-run speed screen. Percentages are time-delta-weighted sample spans for the active execution worker, including sampled idle/wait time; they are diagnostic, not speed comparisons.

| Route | Generated code self | Outer execution loop self |
|---|---:|---:|
| sky | 41.70% | 26.95% |
| combat | 42.31% | 25.47% |
| standard | 53.63% | 15.66% |

InterpreterMainLoop contains the inlined compiled runner. The independent matching V26 censuses recorded zero interpreted guest instructions; the frame name must not be labeled interpreter fallback. Generated code and shared dispatch remain substantial costs. One ROM generated function accounts for about 12.4% of both Sky Force spans, and remains a separate investigation target; dispatch alone is not assumed sufficient for realtime.

Next experiment: iterative module-local immutable-ROM dispatch, preserving exact instruction and constituent-block limits and returning to the outer runner at callbacks, pending syscalls, or RAM successors. Test it on both games with no guest-specific rules. Retain zero-fallback coverage as a separate gate; a changed dispatch-count definition must be reported explicitly.

Latest counter-free V26 speeds (dd2b51b73): Sky Force 0.517x stationary / 0.563x moving-firing; Snakes 1.503x standard / 1.568x longer. No realtime or promotion claim.
