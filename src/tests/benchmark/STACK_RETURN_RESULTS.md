# Caller-specific stack returns

Tracking the caller's return address works, including a hot Sky Force function
that the previous direct-chain experiment could not connect. It does not
establish a useful game-speed gain. The prototype is archived in
[STACK_RETURN_EXPERIMENT.patch](STACK_RETURN_EXPERIMENT.patch); active runtime
source and defaults remain on the control implementation.

## What the prototype does

For a direct ARM `BL`, the continuation is statically known. The analysis accepts
a matching `STMDB sp!, {..., lr}` / `LDMIA sp!, {..., pc}` frame when every interior
instruction leaves SP and LR alone, internal branches stay inside that frame,
and there are no nested calls or unsupported instructions. Conditional branches
and loops are allowed. This is generic instruction analysis, with no game name,
guest address or library-name test in the compiler.

The callee is compiled as a private function in the caller's WASM module. The
caller publishes its register state and calls it directly. The callee returns
a private success code when it reaches that caller's continuation; the caller
reloads its state and continues without returning through the C++ dispatcher.
There is no JS crossing at this call or return.

```text
stock:
    caller: LR = continuation; PC = callee; return to dispatcher
    dispatcher: lookup callee; indirect WASM call
    callee: save LR; run body; restore PC from stack; return to dispatcher
    dispatcher: lookup continuation; indirect WASM call

candidate:
    caller: LR = continuation; PC = callee; publish CPU state
    status = direct_wasm_call(private_callee_for_this_continuation)
    reload CPU state
    if status != returned_to_continuation: return to dispatcher
    execute continuation
```

If no interior store can overwrite the saved LR, the successful return needs no
target comparison. A callee containing arbitrary stores gets one loaded-PC
comparison: those stores might alias the saved return slot. A mismatched target,
including an ARM-to-Thumb return, takes the normal dispatcher exit. Actually
executed memory helpers and pending interrupts also exit normally. Existing
loop watchdog checks remain; this does not assume that all loops terminate.
The experiment uses the existing unsafe-code default and does not extend the
strict self-modifying-code policy.

This is direct-call specialization, **not whole-body inlining**. It retains
state publication/reload, stack memory operations and the callee's existing
memory lowering. Caller entry-memory proofs are disabled when a private call
could invalidate their register assumptions. Callees requiring another level
of private outlining are rejected by the current module builder.

## Which hot functions it reaches

A normally tiered Sky Force gameplay capture contains nine compiled private
copies, representing three unique callee/continuation pairs. The actual captured
WASM contains a direct `call` to the new private callee. The hottest direct
callee, the pixel converter at `0x700042fc`, contributes 56 CPU samples
in its private function across two captured code versions. Its direct call is
at `0x70009090`, with continuation `0x70009094`. These are function samples,
not counts of call or return instructions.

The other four hot stack-returning functions (`0x700032bc`, `0x70003bc4`,
`0x70003638`, `0x70003718`) appear in a game function-pointer table. The direct
call-site analysis does not select them. Linking those paths requires resolving
or guarding the indirect callee target as well as its return. A virtual callback
in EUser remains outside this prototype too.

## Exact-work measurement

The benchmark runs the captured 29-instruction converter through the **real
C++ `execute_chain` runner**, real translator, real generated WASM and registry.
Each observation converts 1,000 identical 240-by-320 images. It checks the final
guest state, output endpoints, completion count and absence of memory helpers.
Unlike the game-time windows below, this fixes the amount of guest work.

Chrome 153.0.8010.52 / V8 15.3.76.13 uses normal tiering, with no forced TurboFan
and no sampling. JIT logging is enabled. CPU 7 and sibling 15 are reserved;
the requested clock is 2.4 GHz and all eight observations measure about
2394.4 MHz. Hardware counters are not multiplexed. The micro driver checks
whole-window frequency; it does not claim the game's interval/sibling validation.
Order is ABBA then BAAB after warmup, four observations per variant.

| # | Measurement | Stock | Caller-specific return | Difference |
|---:|---|---:|---:|---:|
| 1 | Mean worker CPU time | 699.181 ms | 698.418 ms | +0.109% CPU throughput |
| 2 | Mean retired native instructions | 7,157,289,337 | 7,156,831,205 | -0.0064% |

All four adjacent pairs use less candidate CPU time, ranging from +0.046% to
+0.219% throughput. This is a very small local effect, not a game-speed claim.
The fixture also links its surrounding caller loop, so its reduction is not
an isolated cost measurement of the callee's return alone.

The converter is hot because it processes a whole image: its inner pixel loop
runs 38,400 times per invocation. Removing a call/return pair once per image
removes very little of its total work. A high function CPU share does not imply
that the function crosses a dispatcher boundary frequently.

## Game measurements and limitations

The control is `8d000d8fb`, with runtime SHA-256
`a62e6b920d773d1c3f14dd2213fe28d8a1d3f64e8165b4309cee797138f29597`.
The candidate runtime is
`dc70ea0e58efcc04ec1167922720cbeb8a0259a1e623dc07e6fcb8365985bc23`.
Both use the stock game assets, direct memory, unsafe-code mode 3, a 2 ms
watchdog, shared audio, hardware graphics and no detailed custom diagnostics.
The production LAN service continues to serve its separate frozen build.

The Snakes screen did not complete a comparable ABBA. The initial long window
ended at a menu and was rejected before running a candidate. In a shorter
42-46 guest-second window, the clock-valid control used 0.831104 CPU seconds
and a clock-valid candidate 0.826961 seconds, but they presented 66 and 64
frames. Another candidate failed sibling isolation; a later one reached a menu
and recorded a package throttle event. These observations remain in the raw
evidence. They do not establish a Snakes gain or regression.

A Sky Force control at a requested 3.6 GHz averaged only about 1.6 GHz. That
campaign stopped immediately. A fresh comparison used 2.4 GHz after the host
held that frequency in the exact-work test; observations at different requested
frequencies are not pooled. No temperature ceiling or cooldown is used.

The final Sky Force ABBA uses 58-76 guest seconds. All four observations pass
measured-frequency, interval, sibling-isolation, counter and throttle rules:

| # | Order / variant | Worker CPU time | Native instructions | Presented frames | Final score / stage |
|---:|---|---:|---:|---:|---|
| 1 | A / stock | 3.689936 s | 23.2464 billion | 565 | 825 / 4% |
| 2 | B / candidate | 3.796728 s | 24.3746 billion | 567 | 200 / 1% |
| 3 | B / candidate | 3.593307 s | 23.4148 billion | 567 | 1250 / 4% |
| 4 | A / stock | 3.818540 s | 23.5729 billion | 553 | 200 / 1% |

The arithmetic mean gives +1.60% CPU throughput and
+2.07% native instructions. Pair directions are mixed. **Neither number is a causal
performance claim**: scenes, scores and rendered work differ even between the
two controls. Both variants reach gameplay without an observed abort or WASM
instantiation failure. This is a browser smoke check, not an exact replay.
Repeating this unequal-work window does not resolve a sub-percent effect.

The exact-work converter result and limited call-site coverage do not justify
graduating this version. The implementation is retained as a reproducible patch,
not as another inactive option in production code.

## Correctness and reproduction

The focused suite passes 384 state, memory and callback comparisons and 32
independent interpreter comparisons, covering TLB/direct memory, read-only and
storing callees, conditional paths, unmapped accesses, saved-return aliasing,
Thumb targets, callback state changes and pending interrupts. Actual dispatcher
invocations across the fixture fall from 1,600 to 976. Rejected-frame tests
cover SP/LR writes, nested calls, escaping branches, mismatched pops and invalid
block transfers. The complete candidate compiler suite reports 182 passed,
zero failed.

The patch includes the compiler implementation, focused fixture and browser
micro driver. Apply it to this report's baseline, then build:

```sh
cmake --build build-wasm --target test_aot_wasm eka2l1_wasm --parallel 6
node build-wasm/src/tests/aot/test_aot_wasm.js --stack-returns-only
node build-wasm/src/tests/aot/test_aot_wasm.js
```

Use the existing [fixed-frequency wrapper](CONTROLLED_BENCHMARKS.md), reserving
CPU 7 and sibling 15 with a fresh restoration file, around:

```sh
node src/tests/benchmark/stack_returns/micro.mjs \
  build-wasm/src/tests/aot /ABS/NEW_OUTPUT 7 2400 2304
```

The last arguments are the reserved CPU, requested MHz and calibrated reference
MHz. There is no production browser selector; build the unpatched control and
patched candidate separately. The test-only `arm_stack_returns` policy selects
both compiled variants inside the micro fixture.

[Machine-readable evidence](STACK_RETURN_RESULTS.json) records the observations,
clock validation, coverage, hashes and host-restoration status. Frozen runtime
and test binaries, source snapshots, all failed attempts, screenshots, generated
WASM and raw profiles remain under
`/home/claude/.scratch/eka-stack-return-20261010`. The preserved `screen5.py` and
plan reproduce the final game comparison against frozen artifacts.

The restored runtime rebuilds to exactly the control WASM SHA-256 above. The
production LAN service remains active and returns HTTP 200. All eight
host-control invocations report successful restoration, including the early
failures before measurement. The archived patch passes `git apply --check`.
The final preserved candidate binary independently repeats the 182-test pass.
The rebuilt control suite reports 181 passed, zero failed.
