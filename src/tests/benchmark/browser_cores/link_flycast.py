"""Link the pinned, patched Flycast CPU adapter without a console frontend."""
from pathlib import Path
import argparse, subprocess
p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);a=p.parse_args()
w=a.workspace;b=w/'flycast-source/build-cpu-bench';out=w/'browser-cores-nonarm'
exports=['bench_setup','bench_run','bench_result','wasm_mem_read8','wasm_mem_read16','wasm_mem_read32','wasm_mem_write8','wasm_mem_write16','wasm_mem_write32','wasm_exec_ifb','wasm_exec_shil_fb','wasm_sq_pref','wasm_div32u','wasm_div32s','wasm_div1']
args=[str(w/'emsdk/upstream/emscripten/em++'),'-O3','--no-entry','-fexceptions','-sDISABLE_EXCEPTION_CATCHING=0','-sMODULARIZE=1','-sEXPORT_NAME=createFlycast','-sALLOW_MEMORY_GROWTH=1','-sALLOW_TABLE_GROWTH=1','-sINITIAL_MEMORY=268435456','-sEXPORTED_FUNCTIONS='+str(['_'+x for x in exports]),'-sEXPORTED_RUNTIME_METHODS=["wasmExports","HEAPU8"]','-Wl,--start-group']
args += [str(f) for f in b.rglob('*.a')]
args += ['-Wl,--end-group','-sFULL_ES3=1','-sMIN_WEBGL_VERSION=2','-sMAX_WEBGL_VERSION=2','-o',str(out/'flycast.js')]
subprocess.run(args,check=True)
