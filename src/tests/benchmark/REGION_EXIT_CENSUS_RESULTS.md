# Post-merge compiled-region exit census

This diagnostic uses original-emitter policy 7, folded TLB, grouped scanner,
original cache lookup, and unchanged 512-byte / 16-instruction / eight-site /
512-region limits. Guest scheduling is unchanged. Detailed instrumentation and
overlapping correctness work make diagnostic elapsed times unsuitable for
performance claims. All raw reports, GPU/build/source hashes and samples remain.

| Exact event count | Longer route 42–60s | Standard route 60–78s |
| --- | ---: | ---: |
| region_exits: budget | 623,412 | 611,462 |
| region_exits: call | 65,936,330 | 67,629,684 |
| region_exits: emission_end | 13,468 | 14,274 |
| region_exits: external_direct_branch | 26,003,615 | 25,058,698 |
| region_exits: helper_exit | 53,359 | 78,792 |
| region_exits: indirect_bx | 2,164,577 | 1,817,805 |
| region_exits: indirect_call | 23,050 | 24,207 |
| region_exits: indirect_ldm_pc | 31,033,115 | 31,645,262 |
| region_exits: indirect_load_pc | 12,467,402 | 12,037,854 |
| region_exits: memory_guard | 1,705,945 | 1,699,669 |
| region_exits: other_control | 1,027,088 | 1,024,774 |
| region_exits: other_thumb_control | 40,573 | 43,594 |
| region_exits: return_bx_lr | 36,084,653 | 36,937,957 |
| region_exits: return_pop_pc | 220,443 | 232,561 |
| region_exits: source_window_end | 1,265,393 | 1,329,114 |
| region_exits: thumb_call_half | 4,564,452 | 3,992,939 |
| region_exits: unsupported | 72,813 | 78,035 |
| runner_returns: budget | 623,517 | 611,609 |
| runner_returns: region_cap | 20,604 | 17,881 |
| runner_returns: successor_unavailable | 131,562 | 149,525 |
| runner_returns: zero_progress | 1,142,537 | 1,138,913 |

Zero progress is an orthogonal property, already included in the exit reasons:
1,142,537 longer and 1,138,913 standard.
Neither window records a code/mapping invalidation, interrupt or stop. This is
workload coverage, not permission to remove those checks. Every region return
publishes the original-emitter guest state. Calls/returns and external branches
do not inherently require a guest scheduler/browser yield; the current compiler
makes them boundaries. Memory/helper exits, invalidation, budgets and interrupts
retain their precise contracts.

The runner cap fires only about 1% of runner returns and about 0.01% of compiled
calls. Raising/removing it still pays register publication, lookup and validation
for each region. Source-window ends are below 1% of region returns. Direct calls
and returns dominate. These facts prioritize supported callee/veneer fusion,
without establishing that any such implementation or larger threshold is faster.

Edges are sampled every 1,021 compiled calls; totals above count every call.
There are no dropped edge/site samples. Compilation reasons cover the whole
browser lifetime, not just the measured window, and include repeated translation
of private fallbacks. Site-to-reason correlation can be ambiguous across region
contexts/address spaces; these sampled associations are not exact dynamic counts
of each inlining limit. A follow-up will attach the actual refusal reason to each
emitted call exit. All unknown/other categories remain visible.

The top longer-scene edge is a literal-load veneer at 0x70062ad0 transferring to
0x80191968; other frequent edges include calls from 0x70062ec0 and 0x70064ca4,
and returns from 0x70062ed4 and 0x80191ac8. These are diagnostic targets, not
addresses to special-case in production.

## Scheduling audit

execute_chain returns to InterpreterMainLoop. Positive progress resumes its CPU
dispatch loop; a region-cap return does not return from cpu->run by itself.
system_impl::loop calls cpu->run with the existing deterministic limit of at most
4,840 instructions and then advances timers and asks the guest kernel to
reschedule. The browser frontend runs this system loop on a worker; pacing and
graphics synchronization are separate. A synchronous JS helper is neither a
necessary nor sufficient condition for returning to guest scheduling.

## Verification and preserved work

The actual ARM/Thumb exit-label fixtures pass, including budget, call/return,
unsupported, interrupt, deferred memory, helper and physical code-alias exits.
The complete instrumented compiler suite passes all 165 cases (the pre-existing
documented XFAIL remains). This validates state/memory semantics under emitted
diagnostic stores; the cache invalidation counters were subsequently separated
without changing validation decisions. No new emulator deployment or speed claim.

The archived IR-budget-gap implementation separately passes 27,712 explicitly
selected native fault comparisons and both exact 1,600-image/4,656,051-PCM-frame
replays plus the longer-route replay. Its first suite had a coverage-fixture
failure: LDM/STM were already supported by IR. The corrected conditional-memory
fixture selects both IR and budget chunks and passes 7,680 adversarial comparisons
in the instrumented suite. The initial failure remains. No IR timing was started.
