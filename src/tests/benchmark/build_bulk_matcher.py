#!/usr/bin/env python3
"""Extract the production Thumb recognizer unchanged for side-effect-free snapshot analysis."""
from pathlib import Path
import argparse, subprocess
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[3]
s=(root/'src/emu/cpu/src/dyncom/arm_dyncom_interpreter.cpp').read_text()
part=s[s.index('enum accel_chain_op'):s.index('// Execute up to `want`')]
pre='''#include <cstdint>\n#include <cstring>\n#include <algorithm>\n#include <vector>\n#include <iostream>\n#include <stdexcept>\nstruct ARMul_State { uint32_t base; std::vector<uint8_t> bytes; uint32_t ReadCode(uint32_t a) { if(a<base || a-base+4>bytes.size()) throw std::out_of_range("code"); uint32_t w; memcpy(&w,bytes.data()+a-base,4); return w; }};\n'''
post='''\nint main(){uint32_t base; std::string hex; while(std::cin>>base>>hex) { ARMul_State c{base,{}}; for(size_t i=0;i<hex.size();i+=2)c.bytes.push_back(std::stoul(hex.substr(i,2),nullptr,16)); for(size_t i=0;i+2<c.bytes.size();i+=2){loop_accel_inst out;try{if(analyze_thumb_bulk_loop(&c,base+i,out))std::cout<<base+i<<":"<<out.body_len<<",";}catch(const std::out_of_range&){} } std::cout<<"\\n"; }}\n'''
source=a.output.with_suffix('.cpp');source.write_text(pre+part+post)
subprocess.run(['c++','-O2',str(source),'-o',str(a.output)],check=True)
# Known fill loop must match; a non-loop must not.
assert subprocess.check_output([str(a.output)],input='4096 08600431013afbd1\n',text=True).strip()=='4096:4,'
assert subprocess.check_output([str(a.output)],input='4096 0020704700000000\n',text=True).strip()==''
