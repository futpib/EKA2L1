// Offline ordinary-memory comparison. No emulator shortcuts are installed.
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/frontend/A32/a32_ir_emitter.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
struct Fixture : Dynarmic::A32::UserCallbacks {
 std::vector<std::uint8_t> memory=std::vector<std::uint8_t>(0x20000), code;
 std::uint32_t pc; std::uint64_t ticks=0, limit=0; Dynarmic::TLB<9> tlb{12};
 template<class T>T read(unsigned a){if(a+sizeof(T)>memory.size())throw std::runtime_error("read");T v;std::memcpy(&v,memory.data()+a,sizeof(v));return v;}
 template<class T>void write(unsigned a,T v){if(a+sizeof(T)>memory.size())throw std::runtime_error("write");std::memcpy(memory.data()+a,&v,sizeof(v));}
 std::optional<unsigned> MemoryReadCode(unsigned a)override {if(a==0x1a000)return 0xeafffffe; if(a<pc||a-pc+4>code.size())return {};unsigned v;std::memcpy(&v,code.data()+a-pc,4);return v;}
 bool PreCodeReadHook(bool,unsigned a,Dynarmic::A32::IREmitter& ir)override{if(a==pc+code.size()||a==0x1a000){ir.BranchWritePC(ir.Imm32(a));ir.SetTerm(Dynarmic::IR::Term::ReturnToDispatch{});return false;}return true;}
 std::uint8_t MemoryRead8(unsigned a)override{return read<std::uint8_t>(a);}
 std::uint16_t MemoryRead16(unsigned a)override{return read<std::uint16_t>(a);}
 std::uint32_t MemoryRead32(unsigned a)override{return read<std::uint32_t>(a);}
 std::uint64_t MemoryRead64(unsigned a)override{return read<std::uint64_t>(a);}
 void MemoryWrite8(unsigned a,std::uint8_t v)override{write(a,v);}void MemoryWrite16(unsigned a,std::uint16_t v)override{write(a,v);}
 void MemoryWrite32(unsigned a,std::uint32_t v)override{write(a,v);}void MemoryWrite64(unsigned a,std::uint64_t v)override{write(a,v);}
 void InterpreterFallback(unsigned,size_t)override{throw std::runtime_error("fallback");}void CallSVC(unsigned)override{throw std::runtime_error("svc");}
 void ExceptionRaised(unsigned pc,Dynarmic::A32::Exception e)override{throw std::runtime_error("exception at "+std::to_string(pc)+" kind "+std::to_string(int(e)));}
 void AddTicks(std::uint64_t n)override{ticks+=n;}std::uint64_t GetTicksRemaining()override{return ticks>=limit?0:limit-ticks;}
 void init(Dynarmic::A32::Jit& jit,unsigned seed,unsigned budget){
  std::fill(memory.begin(),memory.end(),0);auto x=seed;
  for(unsigned a=0x10000;a<0x19000;a+=4){x=x*1664525+1013904223;write(a,x);}
  auto &r=jit.Regs();for(unsigned i=0;i<16;++i)r[i]=0x13000+i*64;
  r[0]=0x12000;r[1]=0x10000;r[2]=0x11000;r[4]=0x13000;r[5]=0x14000;r[13]=0x18000;r[14]=0x1a000;r[15]=pc;
  jit.SetCpsr(0x10);ticks=0;limit=budget;
  for(unsigned a=0x10000;a<=0x19000;a+=4096)tlb.Add(a,memory.data()+a,Dynarmic::MemoryPermission::ReadWrite);
 }
};
int main(int argc,char**argv){
 if(argc<3||argc>4)return 1;std::filesystem::create_directory(argv[2]);
 if(std::getenv("EKA_KERNEL_DUMP"))setenv("PERF_BUILDID_DIR",argv[2],1);
 std::ofstream report(std::string(argv[2])+"/native.json");report<<"{\"kernels\":[";bool comma=false;
 for(auto [pc,cycles]:{std::pair{1879455500u,57u},std::pair{1879129820u,7u},std::pair{0x70000000u,1u}}){
  Fixture f;f.pc=pc;std::ifstream in(std::string(argv[1])+"/"+std::to_string(pc)+".arm",std::ios::binary);f.code.assign(std::istreambuf_iterator<char>(in),{});
  if(pc==0x70000000u)f.code={0,0,0xa0,0xe1};
  if(f.code.empty())return 2;
  Dynarmic::A32::UserConfig cfg;cfg.callbacks=&f;cfg.tlb_entries=f.tlb.entries.data();cfg.arch_version=Dynarmic::A32::ArchVersion::v6T2;cfg.define_unpredictable_behaviour=true;if(argc==4){if(std::string(argv[3])!="--fastmem")return 3;cfg.fastmem_pointer=f.memory.data();}
  Dynarmic::A32::Jit jit(cfg);auto prefix=std::string(argv[2])+"/"+std::to_string(pc);
  // Step is the precise-state oracle; Run is separately checked at full budget.
  for(unsigned seed=1;seed<=16;++seed)for(unsigned budget=0;budget<=cycles;++budget){
   f.init(jit,seed,budget);for(unsigned i=0;i<budget;++i)jit.Step();
   std::ofstream out(prefix+"-"+std::to_string(seed)+"-"+std::to_string(budget)+".bin",std::ios::binary);
   out.write(reinterpret_cast<char*>(jit.Regs().data()),64);auto cpsr=jit.Cpsr();out.write(reinterpret_cast<char*>(&cpsr),4);out.write(reinterpret_cast<char*>(&f.ticks),8);out.write(reinterpret_cast<char*>(f.memory.data()),f.memory.size());
   if(budget==cycles){auto regs=jit.Regs();auto mem=f.memory;f.init(jit,seed,cycles);jit.Run();if(jit.Regs()!=regs||f.memory!=mem||f.ticks!=cycles||jit.Cpsr()!=cpsr){std::cerr<<"Run ticks "<<f.ticks<<" cpsr "<<jit.Cpsr()<<" expected "<<cpsr<<" memory "<<(f.memory==mem)<<"\n";for(int k=0;k<16;++k)if(regs[k]!=jit.Regs()[k])std::cerr<<k<<" "<<std::hex<<regs[k]<<" "<<jit.Regs()[k]<<std::dec<<"\n";throw std::runtime_error("Run/Step mismatch");}}
  }
  f.init(jit,72,cycles);auto initial=jit.Regs();for(unsigned i=0;i<500000;++i){jit.Regs()=initial;f.ticks=0;jit.Run();}
  if(comma)report<<',';comma=true;report<<"{\"pc\":"<<pc<<",\"cycles\":"<<cycles<<",\"calls\":5000000,\"ms\":[";
  for(unsigned round=0;round<6;++round){auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<5000000;++i){jit.Regs()=initial;f.ticks=0;jit.Run();}auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(round)report<<',';report<<ms;}
  report<<"]}";
  if(std::getenv("EKA_KERNEL_DUMP")){
   std::ifstream map(std::string(argv[2])+"/perf-"+std::to_string(getpid())+".map");std::string line;unsigned index=0;
   while(std::getline(map,line)){std::istringstream row(line);std::uintptr_t address,size;std::string name;row>>std::hex>>address>>size>>name;
    if(name.rfind("a32_a",0)==0&&std::stoul(name.substr(5,8),nullptr,16)==pc){std::ofstream out(prefix+"-native-"+std::to_string(index++)+".bin",std::ios::binary);out.write(reinterpret_cast<char*>(address),size);}
   }
  }
  std::cout<<pc<<" completed\n";
 }
 report<<"]}\n";
}
