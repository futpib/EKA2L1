#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/dyncom/arm_dyncom_interpreter.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/aot/counted_js.h>
#include <cpu/aot/loop_js.h>
#include <cpu/aot/region_js.h>
#include <cpu/aot/direct_js.h>
#include <cpu/aot/word_increment.h>
#include <cpu/aot/semantic_js.h>
#include <emscripten/emscripten.h>
#include <common/log.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace eka2l1::arm;
namespace eka2l1::arm { struct matched_kernel_access { static ARMul_State *state(dyncom_core &c) {return c.state_.get();} }; }
struct Region {std::uint32_t base,permissions;std::vector<std::uint8_t> bytes;};
struct Reference {
 std::atomic<std::uint64_t> generation{1};r12l1::exclusive_monitor monitor{1};dyncom_core cpu{&monitor,12};
 std::vector<Region> maps;std::vector<std::uint8_t> events,seed;std::uint32_t fault=0,fault_address=0,address_space=1,deny_begin=0,deny_end=0,deny_permissions=0,helper_irq_after=0,helper_accesses=0,resolves=0;bool cache_pages=false;
 Region *find(std::uint32_t a,std::size_t n,unsigned permission){for(auto &m:maps)if(a>=m.base&&n<=m.bytes.size()&&a-m.base<=m.bytes.size()-n&&(m.permissions&permission)==permission)return &m;return nullptr;}
 void seed_page(unsigned a,Region&m){const auto page=a&~4095u;if(page>=m.base&&m.bytes.size()>=4096&&page-m.base<=m.bytes.size()-4096)cpu.set_tlb_page(page,m.bytes.data()+page-m.base,static_cast<prot>(m.permissions));}
 template<class T,bool Trace> bool access(unsigned a,T *value,bool write){if((deny_permissions&(write?2u:1u))&&std::uint64_t(a)<deny_end&&std::uint64_t(deny_begin)<std::uint64_t(a)+sizeof(T))return false;auto *m=find(a,sizeof(T),write?2:1);if(!m)return false;
  if(write)std::memcpy(m->bytes.data()+a-m->base,value,sizeof(T));else std::memcpy(value,m->bytes.data()+a-m->base,sizeof(T));
  if constexpr(!Trace){if(cache_pages)seed_page(a,*m);}
  if constexpr(Trace){events.push_back(write?1:0);const std::uint32_t size=sizeof(T);const std::uint64_t v=*value;for(auto [ptr,n]:{std::pair<const void*,unsigned>{&a,4},{&size,4},{&v,8}}){const auto *b=static_cast<const std::uint8_t*>(ptr);events.insert(events.end(),b,b+n);}}
  if(helper_irq_after&&++helper_accesses==helper_irq_after)matched_kernel_access::state(cpu)->NirqSig=0;
  return true;
 }
 template<bool Trace> void callbacks(){
#define ACCESS(bits,type) cpu.read_##bits##bit=[this](unsigned a,type*v){return access<type,Trace>(a,v,false);};cpu.write_##bits##bit=[this](unsigned a,type*v){return access<type,Trace>(a,v,true);};
 ACCESS(8,std::uint8_t) ACCESS(16,std::uint16_t) ACCESS(32,std::uint32_t) ACCESS(64,std::uint64_t)
#undef ACCESS
 }
 template<bool Trace> void resolver(){cpu.resolve_code=[this](unsigned a,core::code_mapping &view){if constexpr(Trace)++resolves;auto*m=find(a,4,4);if(!m)return false;view.address_space=address_space;view.bytes=m->bytes.data()+a-m->base;view.size=m->bytes.size()-(a-m->base);return true;};}
 void data_resolver(){cpu.resolve_data=[this](unsigned a,bool write,core::data_mapping &view){
  view={};auto*m=find(a,1,write?3:1);if(!m||(m->permissions&4))return false;
  const auto end=std::min<std::uint64_t>(std::uint64_t(m->base)+m->bytes.size(),(std::uint64_t(a)|4095u)+1);
  if((deny_permissions&(write?2u:1u))&&std::uint64_t(a)<deny_end&&std::uint64_t(deny_begin)<end)return false;
  const auto host=reinterpret_cast<std::uintptr_t>(m->bytes.data()+a-m->base);
  for(const auto&code:maps)if(code.permissions&4){const auto begin=reinterpret_cast<std::uintptr_t>(code.bytes.data());if(host<begin+code.bytes.size()&&begin<host+end-a)return false;}
  view.address_space=address_space;view.bytes=reinterpret_cast<std::uint8_t*>(host);view.size=end-a;return true;
 };}
 Reference(){auto *s=matched_kernel_access::state(cpu);s->Spsr.fill(0);s->Spsr_copy=0;cpu.set_cpsr(USER32MODE);cpu.set_asid(1);cpu.code_mapping_generation=&generation;
  callbacks<true>();
  cpu.read_code=[this](unsigned a,unsigned*v){auto *m=find(a,4,4);if(!m)return false;std::memcpy(v,m->bytes.data()+a-m->base,4);return true;};
  resolver<false>();data_resolver();
  cpu.exception_handler=[this](exception_type type,unsigned a){fault=static_cast<unsigned>(type)+1;fault_address=a;cpu.stop();return false;};
  cpu.system_call_handler=[this](unsigned n){fault=0x80000000u|n;cpu.stop();};
 }
};
static std::unique_ptr<Reference> reference;
static std::string result;
static Reference &get(){if(!reference)throw std::runtime_error("No reference fixture");return *reference;}
extern "C" {
EMSCRIPTEN_KEEPALIVE void ref_create(){if(!eka2l1::already_setup)eka2l1::log::setup_log(nullptr);reference=std::make_unique<Reference>();}
EMSCRIPTEN_KEEPALIVE void ref_destroy(){reference.reset();}
EMSCRIPTEN_KEEPALIVE std::uint8_t *ref_map(unsigned base,unsigned size,unsigned permissions){auto &f=get();if(!size||size>32*1024*1024||std::uint64_t(base)+size>0x100000000ull||!(permissions&1)||permissions&~7u)throw std::runtime_error("Invalid map");for(const auto&m:f.maps)if(std::uint64_t(base)<std::uint64_t(m.base)+m.bytes.size()&&std::uint64_t(m.base)<std::uint64_t(base)+size)throw std::runtime_error("Overlapping map");f.maps.push_back({base,permissions,std::vector<std::uint8_t>(size)});return f.maps.back().bytes.data();}
EMSCRIPTEN_KEEPALIVE void ref_register(unsigned i,unsigned v){if(i>=16)throw std::runtime_error("Invalid register");get().cpu.set_reg(i,v);}
EMSCRIPTEN_KEEPALIVE void ref_cpsr(unsigned value){get().cpu.set_cpsr(value);}
EMSCRIPTEN_KEEPALIVE ARMul_State *ref_state(){return matched_kernel_access::state(get().cpu);}
EMSCRIPTEN_KEEPALIVE void ref_trace(unsigned enabled){auto &f=get();if(enabled)f.callbacks<true>();else f.callbacks<false>();}
EMSCRIPTEN_KEEPALIVE unsigned ref_run(unsigned budget){auto &f=get();counted_js::enabled=loop_js::enabled=region_js::enabled=word_increment::enabled=direct_js::enabled=false;f.cpu.run(budget);return f.cpu.get_num_instruction_executed();}
EMSCRIPTEN_KEEPALIVE unsigned ref_counted_run(unsigned budget){auto &f=get();loop_js::enabled=region_js::enabled=word_increment::enabled=direct_js::enabled=false;if(!counted_js::enabled)throw std::runtime_error("Counted route is not configured");f.cpu.run(budget);return f.cpu.get_num_instruction_executed();}
EMSCRIPTEN_KEEPALIVE unsigned ref_semantic_run(unsigned budget){auto &f=get();region_js::enabled=word_increment::enabled=direct_js::enabled=false;if(!counted_js::enabled||!loop_js::enabled)throw std::runtime_error("Selected route is not configured");f.cpu.run(budget);return f.cpu.get_num_instruction_executed();}
EMSCRIPTEN_KEEPALIVE unsigned ref_semantic_direct(unsigned budget,unsigned function){return semantic_js::execute(matched_kernel_access::state(get().cpu),budget,function);}
EMSCRIPTEN_KEEPALIVE unsigned ref_original_run(unsigned budget){const bool enabled=counted_js::enabled;const auto result=ref_run(budget);counted_js::enabled=enabled;return result;}
EMSCRIPTEN_KEEPALIVE unsigned ref_cache_size(){return ref_state()->instruction_cache.size();}
EMSCRIPTEN_KEEPALIVE unsigned ref_cache_top(){return ref_state()->trans_cache_buf_top;}
EMSCRIPTEN_KEEPALIVE unsigned ref_epoch(){return *direct_js::epoch_address();}
EMSCRIPTEN_KEEPALIVE unsigned ref_forget_entry(unsigned pc){auto*s=ref_state();const auto key=s->make_instruction_cache_key(pc);const auto removed=s->instruction_cache.erase(key);auto&slot=s->block_l1_cache[ARMul_State::block_l1_index(key)];if(slot.key==key)slot.key=ARMul_State::BLOCK_L1_EMPTY;return removed;}
EMSCRIPTEN_KEEPALIVE void ref_enable_tlb(unsigned enabled){auto&f=get();f.cpu.mem_cache()->flush();f.cache_pages=enabled!=0;if(enabled)for(auto&m:f.maps)for(std::uint64_t page=(std::uint64_t(m.base)+4095)&~4095ull;page+4096<=std::uint64_t(m.base)+m.bytes.size();page+=4096)f.seed_page(static_cast<unsigned>(page),m);}
EMSCRIPTEN_KEEPALIVE unsigned ref_tlb_tag(unsigned a,unsigned permission){auto*t=get().cpu.mem_cache();const auto&e=t->entries[t->index(a)];return permission==1?e.read_addr:permission==2?e.write_addr:e.execute_addr;}
EMSCRIPTEN_KEEPALIVE void ref_trace_resolve(unsigned enabled){auto&f=get();f.resolves=0;if(enabled)f.resolver<true>();else f.resolver<false>();}
EMSCRIPTEN_KEEPALIVE unsigned ref_resolves(){return get().resolves;}
EMSCRIPTEN_KEEPALIVE void ref_asid(unsigned asid){auto &f=get();f.address_space=asid;f.cpu.set_asid(asid);}
EMSCRIPTEN_KEEPALIVE void ref_generation(){++get().generation;}
EMSCRIPTEN_KEEPALIVE unsigned ref_remap(unsigned base){auto&f=get();for(auto&m:f.maps)if(m.base==base){auto bytes=m.bytes;m.bytes.swap(bytes);f.cpu.flush_tlb();++f.generation;return reinterpret_cast<std::uintptr_t>(m.bytes.data());}throw std::runtime_error("Missing remap region");}
EMSCRIPTEN_KEEPALIVE void ref_clear_code(){get().cpu.clear_instruction_cache();}
EMSCRIPTEN_KEEPALIVE void ref_invalidate_code(unsigned base,unsigned size){get().cpu.imb_range(base,size);}
EMSCRIPTEN_KEEPALIVE void ref_helper_irq(unsigned after){auto&f=get();f.helper_irq_after=after;f.helper_accesses=0;f.cache_pages=false;f.cpu.flush_tlb();}
EMSCRIPTEN_KEEPALIVE void ref_irq(unsigned asserted){ref_state()->NirqSig=asserted?0:1;}
EMSCRIPTEN_KEEPALIVE void ref_deny(unsigned begin,unsigned end,unsigned permissions){if(end<begin||permissions&~3u)throw std::runtime_error("Invalid denied range");auto&f=get();f.deny_begin=begin;f.deny_end=end;f.deny_permissions=permissions;f.cache_pages=false;f.cpu.flush_tlb();++f.generation;}
EMSCRIPTEN_KEEPALIVE void ref_tlb_page(unsigned address,std::uint8_t *host,unsigned permissions){if(address&4095u||permissions&~7u)throw std::runtime_error("Invalid fixture TLB page");get().cpu.set_tlb_page(address,host,static_cast<prot>(permissions));}
EMSCRIPTEN_KEEPALIVE unsigned ref_fault(){return get().fault;}
EMSCRIPTEN_KEEPALIVE unsigned ref_fault_address(){return get().fault_address;}
EMSCRIPTEN_KEEPALIVE const std::uint8_t *ref_events(){return get().events.data();}
EMSCRIPTEN_KEEPALIVE unsigned ref_events_size(){return get().events.size();}
EMSCRIPTEN_KEEPALIVE void ref_save_seed(){auto &f=get();auto *s=ref_state();const auto *p=reinterpret_cast<const std::uint8_t*>(s);f.seed.assign(p,p+offsetof(ARMul_State,mem_cache_));}
EMSCRIPTEN_KEEPALIVE void ref_restore_seed(){auto &f=get();if(f.seed.size()!=offsetof(ARMul_State,mem_cache_))throw std::runtime_error("Missing seed");std::memcpy(ref_state(),f.seed.data(),f.seed.size());f.fault=f.fault_address=0;f.events.clear();}
EMSCRIPTEN_KEEPALIVE const char *ref_snapshot(){auto*s=ref_state();std::ostringstream o;o<<"{";
 const auto array=[&](const char*key,const auto&values){o<<'"'<<key<<"\":[";bool first=true;for(auto n:values){if(!first)o<<',';first=false;o<<n;}o<<"],";};
 array("Reg",s->Reg);array("Reg_usr",s->Reg_usr);array("Reg_svc",s->Reg_svc);array("Reg_abort",s->Reg_abort);array("Reg_undef",s->Reg_undef);array("Reg_irq",s->Reg_irq);array("Reg_firq",s->Reg_firq);array("Spsr",s->Spsr);array("CP15",s->CP15);array("VFP",s->VFP);array("ExtReg",s->ExtReg);
#define FIELD(n) o<<"\""#n"\":"<<s->n<<',';
 FIELD(Cpsr) FIELD(Spsr_copy) FIELD(phys_pc) FIELD(Mode) FIELD(Bank) FIELD(NFlag) FIELD(ZFlag) FIELD(CFlag) FIELD(VFlag) FIELD(IFFlags) FIELD(shifter_carry_out) FIELD(TFlag) FIELD(NumInstrs) FIELD(NumInstrsToExecute) FIELD(NresetSig) FIELD(NfiqSig) FIELD(NirqSig) FIELD(abortSig) FIELD(NtransSig) FIELD(bigendSig) FIELD(syscallSig)
#undef FIELD
 o<<"\"fault\":"<<get().fault<<",\"fault_address\":"<<get().fault_address<<'}';result=o.str();return result.c_str();}
}
