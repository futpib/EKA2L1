# Conditional pure values in mixed IR

Experimental policy 13 extends policy 12 across conditional pure integer
instructions. It records each predicate from pre-instruction NZCV, translates
the unconditional expression into a trial graph and merges the changed register
and flag values with their preceding values. False conditions still consume
one guest instruction and advance PC. Actual branches remain original-emitter
boundaries, as do conditional memory operations, PC writes and unsupported forms.

The `choose` node's immediate field is explicitly a third value dependency.
Type validation, common-expression keys, liveness and cold exit reconstruction
all include it. WASM select evaluates both arms only for nontrapping pure
integer work. No load, store, memory guard or callback is speculated. Rejected
trial graphs are discarded before code generation. Snapshots consume every
source before overwriting any architectural local.

This is a structural continuation experiment: conditional arithmetic no longer
forces a segment exit and reload before following operations. It does not reuse
the earlier standalone MOV/ALU branchless emitter or assume that its negative
results imply a positive result here. More eager integer work and more selects
can still regress performance.

The new differential matrix covers all 14 conditions, all 16 NZCV states,
edge operands, arithmetic/shifter flags, dependent conditions, memory failures
and every partial budget. Production-runner fault cases exercise conditional
register/flag changes observed by native-matched callbacks. Conditional loads,
stores and control transfers have explicit rejection checks. Existing tests,
physical aliases, remapping and exact image/audio replay remain acceptance gates.

Compare policies 13, 12 and 7 within one archived application and the exact
served policy 7 archive. Keep every sample and exclude owned competing heavy
work from warmup and timing. Until those gates pass and useful gains repeat,
this remains opt-in and does not alter the LAN application.
