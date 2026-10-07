# Instruction budgets at ARM loop heads

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The controlled conditional-exit-only comparison now has four observations per
variant in each game, in ABBA then BAAB order. Snakes is effectively flat:
-0.05% CPU and +0.46% wall throughput, with two of four CPU pairs faster and
almost unchanged native instruction count (-0.002%). Sky Force changes by
-1.44% CPU and -1.11% wall throughput, despite 2.67% fewer retired native
instructions. Only one of its four CPU pairs is faster; the range is -4.95%
to +3.39%. All 16 observations pass the unchanged clock and host checks.
This comparison establishes no gain for the narrower prototype. It does not
identify the cause of the higher cycles per retired instruction. The broader
prototype is also effectively flat for Snakes: -0.27% CPU and -0.18% wall
throughput, with 0.044% fewer native instructions and two of four CPU pairs
faster (-1.36% to +0.95%). Its earlier larger slowdown does not repeat in this
controlled comparison. Sky Force improves by 4.30% CPU and 4.03% wall
throughput, with 2.70% fewer native instructions. All four CPU pairs are faster
(+2.46% to +6.33%), and all eight observations pass the host checks. This
supports the broader prototype's Sky Force gain, at a smaller magnitude than
the original 9.6% wall-throughput estimate.
These results belong to the respective archived policy-7 prototypes and do
not measure a marginal change on today's defaults.

The **browser launcher defaults to compiler policy 17**. It extends policy 7
with one budget proof per eligible loop iteration. `EKA2L1_AOT_IR_MODE=7` selects
the previous behavior explicitly. The measured tradeoff below is accepted for
the default; adoption does not establish a speedup for every game.

The generic rule covers 4–32 contiguous instructions and rejects interior
entries, calls, inlined code and IR segments. Conditional exits are allowed.
A short budget enters a precise compiled fallback. Instruction accounting,
memory and callback checks, interrupts and guest scheduling keep their existing
semantics. See [the design](BUDGET_CHUNKS_DESIGN.md#natural-loop-iterations).

## Gameplay measurements and disposition

All timing observations used fresh Chromium processes, hardware NVIDIA Vulkan,
stock Nokia 5320 assets, deterministic inputs and frozen build snapshots.
Sampling, Chrome tracing and custom diagnostics were disabled. Runs were serial,
in baseline/candidate/candidate/baseline order. Snakes covers 21–25 guest seconds
(644,728,231 instructions, 84 presentations); Sky Force covers moving/firing at
42–48 guest seconds (2,171,043,925 instructions, 192 presentations). Every variant
has identical measured instruction, virtual-time and presentation boundaries.
Warmup is outside these windows and is retained in the evidence.

| # | Selection / game | Baseline seconds | Candidate seconds | Throughput result |
|---|---|---|---|---|
| 1 | Broad loops / Sky Force | 11.3521, 11.6455 | 10.6296, 10.3517 | +9.6% from means; both pairs favor candidate (+6.8%, +12.5%) |
| 2 | Broad loops / Snakes | 3.18155, 2.23753, 2.37344, 2.20751 | 2.33040, 2.58105, 2.41498, 2.36240 | Three of four pairs favor baseline; median throughput −3.5% |
| 3 | Conditional-exit loops only / Sky Force | 11.3165, 10.9970 | 11.0905, 12.0614 | −3.6% from means; pairs disagree |
| 4 | Conditional-exit loops only / Snakes | 2.22121, 2.35596 | 2.32043, 2.33837 | −1.8% from means; pairs disagree |

The first broad Snakes baseline was unusually slow. Its arithmetic mean gives
+3.2% over all eight observations, while the four paired changes are +36.5%,
−13.3%, −1.7% and −6.6%. No sample was discarded. That is a regression signal,
not evidence for a Snakes speedup. The last four Snakes observations reversed
the order (candidate/baseline/baseline/candidate) to check the initial conflict.

The narrower selection required a conditional exit inside the loop, retaining
existing grouping for straight loops. It kept the EUser check reduction but
failed to establish a game-level gain. Policy 17 therefore retains the broader
prototype. The browser launcher now selects it by default; explicit policy 7
retains its original behavior. These small screens do not establish precise
general speed changes.

Timing prototypes temporarily changed policy 7. The final integration places
the broader behavior in policy 17. Its correctness and emitted module were
rechecked after integration; the final binary was not put through another timing
campaign. The reported speed observations belong to the archived prototypes.
Full observations, commands, build hashes and test logs are in
[the evidence record](LOOP_BUDGET_RESULTS.json). Raw artifacts are under
`/home/claude/.scratch/eka-loop-budget`; `conditional` holds the narrower trial,
and `opt-in` holds final integration checks. Baseline source is `ac7f31db9`.

Default adoption was checked through `https://claude-laptop.lan:8188/` in a real
Chromium session using NVIDIA Vulkan. The service's old policy-7 override was
removed, so it uses the source default. Snakes reached gameplay past 23 guest
seconds and Sky Force reached combat past 42; both consumed keyboard input and
continued presenting frames. Runtime logs confirmed `ir_policy=17`, and the
loaded WASM hash matched the verified policy-17 build. The `adoption` evidence
preserves these checks alongside the original opt-in integration record. This
was a deployment check, not another timing campaign.

## Actual emitted code

In the EUser active-object scheduler region at `0x8019d818`, the seven-instruction
loop at `0x8019d81c` through `0x8019d834` changes from **seven individual budget
comparisons to one full-iteration comparison**. Its backedge remains inside
the generated function; the precise short-budget fallback remains available.

The captured module grows from 16,626 to 18,607 bytes, including other exports
and the private fallback. This does not measure V8's optimized machine-code
size or prove which individual instruction consumed sampled CPU time.

## Correctness

- 24,192 comparisons exercise policies 7 and 17 against the
  interpreter's registers, flags, memory and executed instruction count. They
  cover zero/short budgets, repeated six- and seven-instruction loops, prefixes,
  conditional exits inside/outside the parent region, alignment, page boundaries,
  permissions, endianness, code overlap, pending region exits and IRQ backedges.
  Adding an interior entry to an otherwise eligible loop rejects hoisting.
  Straight-loop selection and policy parsing are also checked.
- The broad prototype passed existing budget-chunk tests for policies 6, 7,
  8 and 16, invariant-write tests for 7 and 8, runner limits, guard publication
  and inline limits. The focused loop matrix was rerun after integration.
- Real Chromium/NVIDIA Vulkan replays of Snakes and moving/firing Sky Force each
  match all 60 reference frames, RGBA pixels, instruction counts, guest timestamps,
  PCM samples and audio events exactly. References previously matched native
  execution and were not regenerated for this change. Final replays select 17
  through the real browser configuration API.
- The server policy validation test accepts 17 and rejects 18. The broad compiler
  run exposed an older synchronous-compilation test assuming diagnostic counters
  were enabled. Counter assertions now depend on that build capability; CPU
  state, registry and table-lifetime checks remain. Its separate rerun passes
  16,672 checks. The broad run was stopped after affected tests passed, during
  unrelated IR matrices; this is not a complete suite pass. The existing
  crash-reproduction fixture remains XFAIL.

Correctness and module-inspection runs overlapped other verification work.
Their elapsed times are excluded from performance comparisons.
