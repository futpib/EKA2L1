#!/usr/bin/env python3
"""Generate offline template instantiations from local guest fixtures (no guest bytes in Git)."""
import pathlib,struct,sys
source,dest=map(pathlib.Path,sys.argv[1:]);out=['// Generated from local benchmark fixtures.\n']
for pc,count in [(1879455500,57),(1879129820,7)]:
    data=(source/f'{pc}.arm').read_bytes()[:count*4]
    words=struct.unpack('<'+'I'*count,data)
    out.append(f'static const u32 code_{pc}[]={{'+','.join(hex(w) for w in words)+'};\n')
    out.append(f'__attribute__((noinline)) static u32 reference_{pc}(ARMul_State *s) {{ Frame f(s,guard_begin,guard_end); bool more=true;\n')
    for w in words:out.append(f'if(more)more=f.instruction<0x{w:08x}>();\n')
    out.append('f.flush();return f.count;}\n')
dest.write_text(''.join(out))
