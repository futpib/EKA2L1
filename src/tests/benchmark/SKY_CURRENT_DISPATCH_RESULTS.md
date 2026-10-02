# Fresh V8 dispatch census

After all sixteen V15b timing observations finished, two separate V8 runs
collected detailed counters and systematic edge samples at stride 997.
Their wall times are diagnostic only and are excluded from all speed claims.

| Route | Compiled invocations | Memory callbacks | Thumb call suffix samples | Thumb POP-PC samples |
| --- | ---: | ---: | ---: | ---: |
| Stationary Sky Force | 193,386,711 | 3,508,191 | 32.70% | 27.10% |
| Moving/firing Sky Force | 178,942,708 | 3,263,271 | 32.58% | 27.07% |

Percentages are sampled boundaries, not CPU time or exact dynamic counts. The
last Thumb ROM halfword is reconstructed from linear bounded block length;
ARM and non-ROM instructions are not classified by this method. Both routes
show negligible Thumb backedge samples, so new Thumb loop compilation is not
supported as the next bottleneck-directed experiment by these counts.

The earlier untouched-live stationary census had 256,394,460 compiled
invocations and about 420 million memory callbacks. The current Thumb memory
work substantially changes the mechanism; using the old census as current
would misdirect further optimization. No causal speed percentage is inferred
from these instrumented counts. Existing normal CPU profiles separately show
substantial generated-code and execution/dispatch work.

Next investigate bounded nonrecursive direct calls between immutable ROM
functions. Calls and returns dominate the sampled Thumb boundaries. Preserve
both long-call halfword stops, total instruction budgets, callback publication
and reload, interrupts and mode transitions. Do not select by game or address.
The prepared natural-ROM selection patch remains unbuilt pending stronger
support; it is not the next selected optimization.

Selected unedited 240x320 native reference frames were sent in Buzz with native
attachments. Both were freshly compared pixel-for-pixel with V8 browser output:
Snakes frame 600 and Sky Force combat frame 120. Provenance is in the evidence.
The realtime goal remains open. Nothing deployed or pushed.
