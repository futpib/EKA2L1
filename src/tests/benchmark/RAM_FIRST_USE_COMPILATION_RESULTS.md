# RAM first-use compilation and Thumb register exchange correctness

The shared opt-in first-use selector is frozen before initialization: 0 preserves sampled compilation (default); 1 compiles missing ROM and RAM entries immediately (rejected V24 coverage experiment); 2 compiles missing RAM entries immediately while retaining ROM hotness filtering. RAM entries still use normal code-mapping/lifetime validation and rejection identity. Capacity bounds, instruction budgets and runner limits remain unchanged. No game or guest address selects the policy.

The experiment exposed two existing Thumb register-exchange errors. BX PC now reads the architectural instruction address plus 4; BX/BLX register and BX LR align ARM destinations before exact-budget returns, matching native DynCom. BLX PC retains native behavior for its unpredictable encoding. The unfixed native matrix and startup trace are preserved in FIRST_USE_COMPILATION_SCREEN_EVIDENCE.json.

Fresh acceptance:

- All 189 compiler tests, including 110 actual first-use/ROM/RAM/budget/rejection/mapping checks and 5,632 new full-state register-exchange checks.
- 5,632 native register-exchange comparisons; 5,376 selected memory faults; 20,736 syscall cases; 17,280 exclusive cases; 4,032 ARM memory cases; 2,880 Thumb memory cases; 1,152 Thumb calls. The instruction probes do not exercise first-use selection; real-runner tests and game replays cover that policy.
- Three native package targets and seven exact image/frame-record/PCM replays: both Sky Force routes normal/checked, both Snakes routes and matching stationary control. Actual selector readback and valid post-initialization change rejection are checked.

The initial first-use experiment filled the 4,096-entry ROM capacity during startup and worsened all four interpreter shares. Mode 2 retains the established immutable-ROM hotness policy. Compilation remains a cost, not an assumed speedup.

Diagnostic counts (not wall-time evidence):

| Route | Guest instructions | Interpreted | Share |
|---|---:|---:|---:|
| sky | 2,142,147,961 | 401 | 0.000019% |
| combat | 2,171,043,925 | 17,828 | 0.000821% |
| standard | 2,964,235,296 | 69,510 | 0.002345% |
| long | 3,031,637,220 | 25,917 | 0.000855% |

A separate diagnostic records every remaining interpreted instruction without address sampling. Its four totals exactly match this census, all sample counts are retained, and all remaining interpreted PCs lie within the reference ROM extent (header base/size checked). Missing-lookup events may be resolved by first-use compilation and are not themselves counts of interpreted instructions. No RAM instructions are interpreted in these measured intervals; ROM fallback remains nonzero.

All interpreter counts match independent total-minus-compiled counters. Measured-route coverage does not prove universal instruction coverage. A separate serial timing screen retains warmup time as well as measured-route throughput. No realtime or promotion claim; live remains unchanged and nothing is pushed.
