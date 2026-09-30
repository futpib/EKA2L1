# Exact validation span census after folded-TLB delivery

A separate diagnostic archive counts every equal_code_bytes request in the
longer-snake window42-60, policy7/TLB1. It makes171,058,327 comparison requests
covering10,455,013,868 requested bytes. The histogram and byte total reconcile
exactly, with no overflow bucket. These are requests, not measured CPU time or
actual bytes read before a mismatch can return early.

| Span bytes | Calls | Share of calls |
| --- | ---: | ---: |
| 24 | 25,114,972 | 14.68% |
| 16 | 22,445,852 | 13.12% |
| 20 | 21,601,202 | 12.63% |
| 4 | 18,812,056 | 11.00% |
| 12 | 13,742,409 | 8.03% |
| 28 | 10,605,240 | 6.20% |
| 44 | 5,461,875 | 3.19% |
| 32 | 5,168,251 | 3.02% |
| 228 | 4,693,606 | 2.74% |
| 8 | 3,045,890 | 1.78% |

Spans of at least64 bytes make36,989,400 requests (21.62% of calls), covering
7,738,915,116 requested bytes (74.02% of total). The largest requested span is
512 bytes. Large scans therefore have meaningful coverage, although the many
small requests still pay call and size-dispatch costs. The previous short-inline
and overlapping-tail experiments failed; this census does not overturn them.

Guest instructions remain2,987,830,398 with720 presentations, exactly matching
the accepted route. No new replay acceptance is claimed. Diagnostic wall time
is excluded from performance evidence. Patch, raw histogram, archive hashes and
mode selection are retained. Instrumentation is removed from production source
after capture; the served folded-TLB archive remains unchanged.

The next narrow experiment groups four16-byte equality checks per loop iteration,
combines their mismatch bits and retains the existing exact tail checks. All
loads must remain within the original span; every primary and dependency byte
still participates. This reduces loop/control work on larger comparisons but
could add work on mismatches or cost more instructions overall. It needs byte-
mutation/alignment tests and ordinary serial gameplay controls before any claim.
