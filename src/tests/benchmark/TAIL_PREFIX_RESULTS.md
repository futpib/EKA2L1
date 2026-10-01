# Register-only tail-prefix experiment

Status: opt-in implementation with focused checks; full acceptance and timings pending. No deployment.

The preceding captured code shows that the hotter branch veneer begins with three register moves. Single-instruction feature32 leaves it unfused. New feature64 accepts a nonempty, bounded register-only integer prefix followed by an unconditional ARM branch. It uses the original emitter with conditional leaves enabled and the retained 512/16/8/512 source/leaf/site/runner limits. It does not recognize game addresses.

Guest values remain in WASM locals across the call and prefix. Every operation, false predicate and terminal branch consumes its exact budget; LR keeps the caller return address. The terminal branch takes the ordinary precise exit even when its destination lies inside the caller source window. Destination validation and scheduling are unchanged. The complete prefix becomes an exact code dependency; target bytes are not assumed. SP/LR/PC operands, memory, status transfers, multiply and nested/internal control flow remain excluded. Pure single-instruction veneers retain their separate opt-in feature32.

Focused tests pass 86,400 exact interpreter comparisons across flags, predicates, register shifts, short budgets, positive/negative/self/in-window branch targets and caller stores aliasing primary/dependency bytes. Selection tests cover exclusions and length boundaries. Native CPU/cache tests pass 570 assertions in33 cases. Native/WASM builds and browser configuration/readback checks pass. A stale frontend negative test initially rejected the newly valid mask64; that failure and correction are retained.

Full compiler and explicit native fault checks are running. Browser replay and any timing follow the currently frozen site-limit diagnostic queue. Performance is unmeasured. No claim is made that removing an entry boundary outweighs the added dependency scan or generated code.
