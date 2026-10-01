# Residual boundary sample audit

This is an offline decode of existing sampled opcodes, not a new timing run. Counts below are samples, not exact event totals. Addresses identify diagnostics only and are not implementation special cases.

## long

| Decoded form | Samples |
| --- | ---: |
| ARM LDMIA SP!,{...,PC} return | 30,438 |
| ARM literal-load veneer LDR PC,[PC,#-4] | 11,846 |
| ARM ADD writing PC (computed jump) | 996 |
| ARM LDREX | 68 |
| Thumb BLX register | 32 |

## standard

| Decoded form | Samples |
| --- | ---: |
| ARM LDMIA SP!,{...,PC} return | 31,101 |
| ARM literal-load veneer LDR PC,[PC,#-4] | 11,372 |
| ARM ADD writing PC (computed jump) | 1,012 |
| ARM LDREX | 72 |
| Thumb BLX register | 31 |

The leading ARM other-control instructions 0x908ff100 and 0x908ff101 are conditional ADDs to PC with shifted register operands. Their multiple observed targets fit computed jump tables. Thumb 0x4790/0x47b8 forms are indirect calls rather than unknown opcodes. These observations refine the sampled description only; original exact counters retain their original categories.

Exclusive-load instructions require their memory/exclusive-monitor semantics. Their small sampled presence does not justify changing that behavior while pursuing call fusion. Literal-load veneers and stack returns are frequent, but fusing them would additionally need exact loaded-target/return validation and all existing memory/fault contracts. The present candidate only extends conditional integer leaves.
