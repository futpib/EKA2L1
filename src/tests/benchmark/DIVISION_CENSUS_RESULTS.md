# Integer division call census

Follow-up runtime experiments: [compact dispatch and division lowering](DISPATCH_AND_DIVISION_RESULTS.md).

Read-only disassembly of the local ROM identifies the frequently sampled
0x80191968 routine as integer division. The earlier long-route CPU profile gives
its generated function 0.323570 sampled seconds (2.608% of worker samples). That
is diagnostic attribution, not a wall-time savings promise.

A separate address-selected diagnostic observes 4,983,222 entries and
430,606,488 executed instructions inside those calls over guest seconds 42–60.
4,714,358 entries return a count of 88. Other lengths include short-budget exits;
entry length alone is not proof of full function completion. Inputs have positive
numerators in this workload: 4,906,617 positive/positive and 76,605
positive/negative sign combinations.

Every 1,021st entry records operands and returned count: 4,880 samples, 139
distinct tuples, no dropped samples. The most frequent tuple is numerator
0x40000000, divisor 0x101e, count 88 (4,095 samples, 83.91%). A common quotient
and remainder therefore recur, but this is **not** yet evidence that all guest
state needed for a result cache repeats or that memoization is safe.

Any accelerated result must preserve clobbered registers, flags, guest PC/LR,
instruction counts, short budgets and exceptional paths. A mathematical divide
alone would not satisfy that contract. The next discriminator measures complete
register/flag input recurrence before considering pure-function result reuse.
No game-specific shortcut or runtime memoization is installed.

Guest work remains exactly 2,987,830,398 instructions and 720 presentations.
The diagnostic build is separate; its elapsed times are excluded from promotion
evidence. Its instrumented sources were restored. LAN remains unchanged.

## Complete-input recurrence discriminator

A second diagnostic retains the last completed call input: R0–R15, CPSR, separate
N/Z/C/V/T fields, and Mode. Completion requires the returned PC/mode to match
the entry LR. It executes every call normally and only counts potential reuse.
Of 4,983,222 entries, 4,893,635 reach that return. Only 2,665 entries repeat the
last completed input exactly; 2,607 also have sufficient instruction budget
(**0.0523% of entries**). Guest instruction totals and presentations remain exact.

Do not implement a whole-register result cache: its observed coverage is too low
to justify per-call comparisons and storage. Repeated operands do not imply
repeated complete inputs. Smaller keys would require proven input/output
dependencies and precise reconstruction; this census does not authorize treating
clobbered registers or flags as irrelevant. No numeric-only shortcut is installed.
The diagnostic sources are restored and no emulator deployment occurs.
