# Registry missing-entry cache

A separate 4096-slot cache records failed ROM registry lookups. The existing
positive cache is unchanged. Registration clears the indexed miss slot;
unregister clears a matching miss; clear empties both caches. RAM mapping and
lifetime checks remain in their existing cache. No game/ROM/address selection
is introduced. The additional storage is allocated once at construction.

The first candidate embedded this storage in the registry object. Its full
WASM suite failed at the registry lifecycle test: disassembly confirms the
containing function reserved 65,600 stack bytes, exceeding the default 65,536.
The initial source, archive, error and disassembly are preserved. Production
storage was moved to a vector; the test oracle and stack limit were unchanged.
The corrected V15b production/native/test builds and all checks are fresh.

All 183 compiler tests pass, including 160,000 registry lifecycle comparisons
against an independent ordered-map oracle, covering collisions, repeated
misses, registration/replacement/removal/clear, null functions and extreme
addresses. All three native CTest targets pass. Production comparisons pass
4,032 ARM fault cases, 1,152 Thumb call cases and 2,880 Thumb memory-fault cases.
Six exact native image/record/PCM replays pass: both Sky Force routes normal
and checked, plus both Snakes routes. Thumb memory is 1; ARM memory, eager ROM
regions and ROM leaf fusion are 0. Checked runs use callback memory.

The fixed sixteen-observation panel compares the matching V14 production
archive against V15b, with reversed orders on all four routes. Original limits,
mode 3, hardware GPU and shared audio remain. Detailed counters, capture and
sampling are off. The failed V15 watcher never reached a timing sample. No
speed or realtime claim follows from correctness. Nothing deployed or pushed.
