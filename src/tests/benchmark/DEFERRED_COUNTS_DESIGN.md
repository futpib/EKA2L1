# Deferred instruction counts inside proved chunks

Experimental policy 8 extends policy 7's write/read and budget proofs. The first
instruction retains the ordinary budget check/increment and private precise
short-budget fallback. Subsequent instructions in a proved straight-line chunk
increase an emitter offset instead of updating the runtime count at every step.
All original instruction, memory, permission, code-alias and exit checks remain.

Cold exits return base count plus the current constant offset without changing
the successful path's representation. A restartable failed access or unsupported
instruction still removes the current instruction before returning. The base is
at least one because chunk entry charged its first instruction; the entry proof
bounds the sum. Conditions consume instructions whether their effects execute or
not, exactly as before.

Fallthrough commits the offset before leaving the chunk, closing forward labels,
or entering another chunk/loop/control instruction. Taken edges skip the lexical
predecessor's pending update. Final fallthrough commits before final label/loop
closure. Existing policies keep an offset of zero and emit their original code.
No default or served policy changes.

The policy repeats both budget/write matrices and asserts deferred updates were
actually selected. Added cases cover taken/untaken forward joins and three chunks
with repeated proved stores, including short-budget exits in later chunks. All
nineteen native fault modes, full compiler/native/frontend tests and checked
native image/audio replays are required before serial gameplay timing. This is
an unmeasured experiment, not a correctness or speedup claim.
