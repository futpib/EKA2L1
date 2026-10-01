#!/usr/bin/env python3
"""Diagnostic full-budget-only control, NOT an emulator optimization.
Removes per-instruction budget comparisons from captured kernels. It must only
be called with at least the full straight-line kernel budget; it cannot resume
partial execution. This separates that simplification from Dynarmic IR gains.
"""
import pathlib,subprocess,re,sys
root=pathlib.Path(sys.argv[1]);bin=pathlib.Path(sys.argv[2])
for f in root.glob('*.arm'):
 wasm=f.with_suffix('.wasm');wat=f.with_suffix('.whole.wat');subprocess.run([str(bin/'wasm-dis'),str(wasm),'-o',str(wat)],check=True)
 s=wat.read_text();budget=re.search(r'\(local.set (\$\w+)\s+\(i32.load offset=848',s)[1]
 s,n=re.subn(r'\(i32.le_u\s+\(local.get '+re.escape(budget)+r'\)\s+\(local.get \$9\)\s*\)', '(i32.const 0)',s)
 if not n:raise RuntimeError('Missing budget checks')
 wat.write_text(s);out=f.with_suffix('.whole.wasm');subprocess.run([str(bin/'wasm-as'),str(wat),'--enable-threads','-o',str(out)],check=True)
 subprocess.run([str(bin/'wasm-opt'),str(out),'-O3','--enable-threads','-o',str(out)],check=True)
 print(f.stem,n,out.stat().st_size)
