#!/usr/bin/env python3
"""Create an isolated Binaryen tool directory that preserves AOT profile symbols.

Usage: dispatch_binaryen_wrapper.py SDK_UPSTREAM NEW_DIRECTORY
Then EM_BINARYEN_ROOT=NEW_DIRECTORY cmake --build build-wasm --target eka2l1_wasm
Only the instrumented build uses this directory. Never replace the SDK tools.
"""
import argparse
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('upstream',type=Path)
p.add_argument('directory',type=Path)
a=p.parse_args()
source=a.upstream.resolve()/'bin';target=a.directory.resolve()/'bin'
target.mkdir(parents=True,exist_ok=False)
for f in source.iterdir():
    if f.name!='wasm-opt':(target/f.name).symlink_to(f)
wrapper=target/'wasm-opt'
wrapper.write_text('#!/usr/bin/env python3\nimport os,sys\na=sys.argv[1:]\n'
    'if any(x.startswith("-O") for x in a): a=["--no-inline=*aot*"]+a\n'
    f'os.execv({str(source/"wasm-opt")!r},["wasm-opt"]+a)\n')
wrapper.chmod(0o755)
