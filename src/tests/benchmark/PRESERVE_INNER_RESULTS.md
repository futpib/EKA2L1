# Preserve existing inner leaf fusion when selecting prefixes

This separate opt-in selection experiment adds feature bit 16 to prefix bit 8 (mode 24). Defaults remain unchanged. It declines an outer prefix if its first nested BL targets a returning leaf already eligible under the selected integer/memory/branch features. The existing standalone callee can then retain that inner fusion. The selected caller and callee still undergo normal exact code/mapping validation, and guest scheduling and all size/count limits remain unchanged.

The preceding prefix census motivates this rule: prefix fusion reduced direct-call exits but exposed more separate returns, slightly increasing total compiled invocations, while dependency validation grew 42–43%. Its timings were negative. This rule is a bounded profitability heuristic, not a guarantee that every remaining prefix is useful. It adds one bounded code lookup and eligibility scan during compilation; it does not recursively discover prefixes or reuse execution results.

The target is decoded from the actual BL displacement, including backward calls. The eligibility probe does not alter global feature selection. Its result only chooses a translation strategy; it is not used as a code-validity proof. A later change in nested code remains subject to the ordinary translated-region and dependency checks.

Focused tests pass 2,496 new exact selection/budget/register/flag/stack comparisons, alongside 36,960 existing prefix comparisons. They cover inactive mode 16, positive and negative BL displacements, missing mappings, unsupported/nonreturning targets, leaf length rejection, recursive-call rejection, and preservation of standalone inner fusion. A new explicit native fault fixture additionally asserts both outer-prefix selection and inner-leaf selection. Full correctness and gameplay timing are pending. No performance or deployment claim.

The frontend initially retained one old assertion that feature value 16 was invalid; the first test run failed on that expectation. Updating it to the new invalid boundary 32 makes the policy test pass. That test-harness failure is retained; it was not an emulator failure.
