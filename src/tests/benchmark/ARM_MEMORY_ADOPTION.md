# ARM short-block inline memory adoption

Eligible ARM short blocks now always use inline memory access, equivalent to the
former `EKA2L1_ARM_MEMORY=1`. The environment variable and the configure/report
exports have been removed. Launchers reject any defined value, including `1`;
remove the variable from existing commands.

The [preceding assessment](ARM_MEMORY_REASSESSMENT.md) motivated this adoption.
Its default-TLB measurements showed 5.95% less worker CPU for Sky Force combat
and 1.31% more for Snakes, comparing two observations per setting per game.
Adoption accepts that measured tradeoff; it does not establish a universal gain.
The checks below validate the fixed behavior, without repeating that timing
campaign.

## Eligibility

The compiler selects the short-block path with
`bounded && cache_registers && !region`. These are translation modes, not guest
addresses or a game-specific whitelist. There is no additional instruction-count
threshold in this condition.

| # | Translation | Behavior and reason |
|---|---|---|
| 1 | Bounded ARM short block with cached registers | Inline memory is fixed on. This path has budget exits and publish/reload barriers for cached state. |
| 2 | Connected ARM region | Already uses its own inline memory path; the retired setting did not control it. |
| 3 | Unbounded or uncached standalone ARM translation | Retains helper access. The short-block optimization is implemented for the bounded, cached execution contract. |
| 4 | Thumb translation | Uses its separate inline memory implementation and configuration. |
| 5 | Interpreted instruction | Uses interpreter memory access; no generated WASM access to optimize. |

Eligibility does not guarantee that every access takes the fast path. Scalar
byte, halfword and word accesses check mapping, permissions, alignment and
endianness. Multi-register transfers can validate a whole span; failed span
checks fall back to the individual access paths. A miss uses the runtime helper.
Short-block stores also use helpers when executable-code write tracking is
required. TLB (`EKA2L1_MEMORY_IMPL=0`) and direct (`2`) remain separate choices.
Subsequent [direct scalar alignment adoption](UNALIGNED_SCALAR_RESULTS.md)
removes natural-alignment checks in direct mode; its whole-access arena range
and fallback page-end checks remain. TLB keeps the original scalar checks.

## Before and after

Simplified ordinary word load in an eligible block, before adoption with the
old setting disabled:

```python
cpu.pc = guest_pc
publish_cached_registers(cpu)
value = runtime_read32(cpu, address)  # WASM-to-WASM call; C++ performs lookup
reload_cached_registers(cpu)
```

With the inline path:

```python
pc_local = guest_pc
host = check_and_translate_inline(address, selected_memory_backend)
if host:
    value = wasm_load32(host)
else:
    publish_cached_registers_and_runtime_state(cpu)
    value = runtime_read32(cpu, address)
    reload_cached_registers_and_runtime_state(cpu)
    callback_happened = True

# After the complete guest instruction, including all LDM/STM transfers:
if callback_happened:
    honor_stop_or_interrupt()
```

The ordinary memory path crosses no JS boundary. The short-block path also
caches PC, budget, CPSR and TLB metadata in WASM locals. Callback barriers publish
and reload that state so a remap, flag change or budget change is visible to
subsequent instructions. Stop and IRQ signals remain uncached.

## Validation

[Evidence and exact commands](ARM_MEMORY_ADOPTION.json) identify the frozen
normal build used for these checks, with detailed diagnostics compiled out.

- ARM memory matrix: 21,146 full-state, memory, budget and callback comparisons,
  using uncached helper translation as the reference for cached inline access.
- Full compiler run reported 166 passed and one callback-fixture failure. That
  fixture left the CPU stopped with an IRQ pending while expecting four
  instructions to execute. Initializing its run budget and inactive IRQ fixed
  the fixture; the focused ARM matrix and callback test then passed together.
  Production code did not change after the full run. The existing crash-repro
  XFAIL and two diagnostics-only skips remain in the recorded log.
- TLB/direct matrix: 471 comparisons, including permissions, remapping, ASID,
  syscall and memory-exception callback behavior.
- Four 60-frame native-reference browser replays: Snakes and Sky Force combat,
  each under TLB and direct, matching images, instruction counts, guest
  timestamps, PCM and audio events exactly.
- Both profiling-harness smoke runs completed the same 16,019,994 guest
  instructions and presentation journal. These short runs only validate the
  harness; their times are not performance evidence.
- Compiler-policy tests passed. Both browser harnesses reject the retired
  variable with values empty, `-1`, `0` and `1` before creating output or launching
  Chromium. Actual browser runs also assert that the removed configure/report
  API is absent.

An additional `eka_cpu_fault_wasm --arm-leaf-memory --unsafe-code=0` run stopped
at its generated-instruction counter assertion. That probe requires the
instrumented runtime: this normal build omits updates to `aot_instructions`.
It is not counted as passing fault-probe evidence. The focused callback matrix
and native game comparisons above ran on the normal build.
