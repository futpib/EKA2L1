# Register-only tail-prefix experiment

Status: opt-in implementation with focused checks; full acceptance and timings pending. No deployment.

The preceding captured code shows that the hotter branch veneer begins with three register moves. Single-instruction feature32 leaves it unfused. New feature64 accepts a nonempty, bounded register-only integer prefix followed by an unconditional ARM branch. It uses the original emitter with conditional leaves enabled and the retained 512/16/8/512 source/leaf/site/runner limits. It does not recognize game addresses.

Guest values remain in WASM locals across the call and prefix. Every operation, false predicate and terminal branch consumes its exact budget; LR keeps the caller return address. The terminal branch takes the ordinary precise exit even when its destination lies inside the caller source window. Destination validation and scheduling are unchanged. The complete prefix becomes an exact code dependency; target bytes are not assumed. SP/LR/PC operands, memory, status transfers, multiply and nested/internal control flow remain excluded. Pure single-instruction veneers retain their separate opt-in feature32.

Focused tests pass 86,400 exact interpreter comparisons across flags, predicates, register shifts, short budgets, positive/negative/self/in-window branch targets and caller stores aliasing primary/dependency bytes. Selection tests cover exclusions and length boundaries. Native CPU/cache tests pass 570 assertions in 33 cases. Native/WASM builds and browser configuration/readback checks pass. A stale frontend negative test initially rejected the newly valid mask64; that failure and correction are retained.

Full compiler and explicit native fault checks are running. Browser replay and any timing follow the currently frozen site-limit diagnostic queue. Performance is unmeasured. No claim is made that removing an entry boundary outweighs the added dependency scan or generated code.

## Compiler and focused fault gates

All 173 instrumented compiler tests pass, including the new 86,400 comparisons; the existing documented XFAIL remains. The new tail-prefix fault fixture passes 5,376 cases each in control, candidate and instrumented candidate modes (16,128 comparisons, with explicit compiler/feature/readback checks). Six missing/wrong/duplicate marker cases are rejected. These focused cases will also occur within the later full matrix; they must not be misreported as distinct coverage.

The planned timing comparison uses control/candidate/live followed by its mirror, then candidate/live/control followed by its mirror, separately on both routes: 24 total observations. All modes retain the same 512/16/8/512 limits. Only feature64 differs in the matching binary; the untouched conditional-only live archive is separate. Passive host telemetry and all samples are retained. Timings start only after full faults and exact native replays pass and all diagnostic/build jobs finish.

## Full correctness acceptance

Both control and candidate pass 46,016 explicitly selected native fault comparisons (92,032 total), including the new prefix fixture. Checked control/candidate and normal candidate standard replays match all1,600 native images, guest records and4,656,051 PCM frames. The checked longer route matches360 images and2,832,756 PCM frames. Actual compiler policy, feature mask, limits and archive hashes are verified. These full matrices include cases also reported by the focused runs; no extra distinct coverage is implied.

No new timing or live/audio acceptance is claimed yet. The previous conditional-only archive stays served.
