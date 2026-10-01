# Remaining costs after folded data-TLB delivery

Fresh serial CPU profiles use the delivered archive and explicit compiler7/TLB1,
with detailed counters disabled, on the short/long routes in guest seconds42-60.
These sampled diagnostic timings are not promotion measurements.

| Guest worker samples | Short | Longer |
| --- | ---: | ---: |
| Generated code and callees, inclusive |45.82%|46.91%|
| InterpreterMainLoop self, including inlined compiled runner |16.99%|16.81%|
| Validated code-cache find self |12.50%|13.57%|
| Exact byte comparison self |10.27%|11.09%|

Inclusive and self scopes must not be added as independent recoverable costs.
One profile per route does not establish a precise causal change in cost shares.
The remaining cache lookup/validation work is material in both scenes.

A concrete next diagnostic examines the recent-entry table of the validated
code cache. Its existing index ignores high PC bits, so addresses separated by
8192 bytes collide. That observation alone does not establish frequent runtime
collisions or useful savings. Count recent hits, live-slot conflicts, dictionary
recovery and mapping refreshes before changing the index. Exact byte/dependency
checks and mapping-generation rules must remain on every existing path.

Separate sampled dispatch edges also show short indirect thunks frequently;
those counts are not CPU-time measurements and do not establish an inlining win.
The source-level cluster plan remains an alternative, not an implemented result.
