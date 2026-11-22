#include <config/config.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/aot/memory_experiment.h>
#include <mem/allocator/std_page_allocator.h>
#include <mem/chunk.h>
#include <mem/control.h>
#include <mem/mmu.h>
#include <mem/process.h>
#include <common/log.h>
#include <emscripten/emscripten.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <limits>
using namespace eka2l1;
static unsigned checks;
#define CHECK(test) do { ++checks; if (!(test)) { std::printf("FAIL %u line %u: %s\n",checks,__LINE__,#test); throw std::runtime_error("MMU authority control failed"); } } while(0)
static void model_controls(mem::mem_model_type model) {
    config::state conf;
    arm::r12l1::exclusive_monitor monitor(1);
    arm::dyncom_core cpu(&monitor,12);
    mem::basic_page_table_allocator allocator;
    auto control=mem::make_new_control(&monitor,&allocator,&conf,12,false,model);
    auto process=mem::make_new_mem_model_process(control.get(),model);
    auto second=mem::make_new_mem_model_process(control.get(),model);
    auto *mmu=control->get_or_create_mmu(&cpu);
    CHECK(mmu->set_current_addr_space(process->address_space_id()));
    auto make=[&](prot permission,void *host=nullptr,bool owned_data=false) {
        mem::mem_model_chunk_creation_info info{};
        info.size=8192; info.flags=mem::MEM_MODEL_CHUNK_REGION_USER_LOCAL|mem::MEM_MODEL_CHUNK_TYPE_NORMAL;
        info.perm=permission; info.host_map=host; info.clear_host_on_commit=false;
        info.host_map_is_owned_data=owned_data;
        mem::mem_model_chunk *chunk=nullptr;
        CHECK(process->create_chunk(chunk,info)==mem::MEM_MODEL_CHUNK_ERR_OK);
        CHECK(chunk && chunk->commit(0,8192)==(model==mem::mem_model_type::flexible?2:8192));
        return chunk;
    };
    arm::core::data_mapping view{};
    auto *data=make(prot_read_write);
    const auto base=data->base(process.get());
    CHECK(cpu.resolve_data(base+17,true,view));
    CHECK(view.address_space==process->address_space_id() && view.bytes==static_cast<unsigned char*>(data->host_base())+17 && view.size==4096-17);
    CHECK(cpu.resolve_data(base+4096,false,view) && view.size==4096);
    // Repeated offsets and access modes exercise warmed metadata. Actual page
    // assignments must invalidate cached permissions and physical backing.
    CHECK(cpu.resolve_data(base+31,false,view));
    CHECK(view.bytes==static_cast<unsigned char*>(data->host_base())+31);
    CHECK(cpu.resolve_data(base+31,true,view));
    CHECK(view.size==4096-31);
    auto *page=mmu->get_page_info(base);
    const auto original=*page;
    page->assign(original.host_addr,prot_read,original.data_backing);
    CHECK(cpu.resolve_data(base,false,view));
    CHECK(!cpu.resolve_data(base,true,view));
    page->assign(original.host_addr,static_cast<prot>(0),original.data_backing);
    CHECK(!cpu.resolve_data(base,false,view));
    CHECK(!cpu.resolve_data(base,true,view));
    page->assign(static_cast<unsigned char*>(original.host_addr)+4096,original.perm,original.data_backing);
    CHECK(cpu.resolve_data(base+31,true,view) && view.bytes==static_cast<unsigned char*>(original.host_addr)+4096+31);
    page->assign(original.host_addr,original.perm,original.data_backing);
    CHECK(cpu.resolve_data(base+31,true,view) && view.bytes==static_cast<unsigned char*>(original.host_addr)+31);
    CHECK(!cpu.resolve_data(0,true,view));
    auto *ro=make(prot_read);
    const auto robase=ro->base(process.get());
    CHECK(cpu.resolve_data(robase,false,view));
    CHECK(!cpu.resolve_data(robase,true,view));
    auto *code=make(static_cast<prot>(prot_read|prot_exec));
    CHECK(!cpu.resolve_data(code->base(process.get()),false,view));
    CHECK(!cpu.resolve_data(code->base(process.get()),true,view));
    auto *external=make(prot_read_write,data->host_base());
    CHECK(!cpu.resolve_data(external->base(process.get()),false,view));
    CHECK(!cpu.resolve_data(external->base(process.get()),true,view));
    CHECK(cpu.resolve_data(base,true,view));
    conf.log_read=true; CHECK(!cpu.resolve_data(base,false,view)); conf.log_read=false;
    conf.log_write=true; CHECK(!cpu.resolve_data(base,true,view)); conf.log_write=false;
    CHECK(mmu->set_current_addr_space(second->address_space_id()));
    CHECK(!cpu.resolve_data(base,false,view));
    CHECK(mmu->set_current_addr_space(process->address_space_id()));
    CHECK(cpu.resolve_data(base,true,view));
    // FBS owns a stable pool allocation beyond the lifetime of its guest chunk.
    // An explicit creator grant admits it without trusting other host mappings.
    void *owned_host=std::aligned_alloc(4096,8192); CHECK(owned_host);
    auto *owned=make(prot_read_write,owned_host,true);
    const auto owned_base=owned->base(process.get());
    CHECK(cpu.resolve_data(owned_base+17,true,view));
    CHECK(view.bytes==static_cast<unsigned char*>(owned_host)+17 && view.size==4096-17);
    CHECK(cpu.resolve_data(owned_base+4096,false,view));
    auto *untrusted_alias=make(prot_read_write,owned_host);
    CHECK(!cpu.resolve_data(untrusted_alias->base(process.get()),true,view));
    auto *owned_ro=make(prot_read,owned_host,true);
    CHECK(cpu.resolve_data(owned_ro->base(process.get()),false,view));
    CHECK(!cpu.resolve_data(owned_ro->base(process.get()),true,view));
    const auto before_decommit=mem::mapping_generation.load();
    owned->decommit(0,4096);
    CHECK(mem::mapping_generation.load()>before_decommit);
    CHECK(!cpu.resolve_data(owned_base,false,view));
    CHECK(cpu.resolve_data(owned_base+4096,true,view));
    CHECK(owned->commit(0,4096)==(model==mem::mem_model_type::flexible?1:4096));
    CHECK(cpu.resolve_data(owned_base,true,view));
    process->delete_chunk(untrusted_alias); process->delete_chunk(owned_ro);
    process->delete_chunk(owned);
    CHECK(!cpu.resolve_data(owned_base,false,view));
    // The creator can retain and reuse the allocation after unpublishing it.
    owned=make(prot_read_write,owned_host,true);
    const auto reused_base=owned->base(process.get());
    CHECK(cpu.resolve_data(reused_base,true,view));
    auto *owned_exec=make(static_cast<prot>(prot_read|prot_exec),owned_host,true);
    CHECK(!cpu.resolve_data(owned_exec->base(process.get()),false,view));
    CHECK(!cpu.resolve_data(owned_exec->base(process.get()),true,view));
    CHECK(!cpu.resolve_data(reused_base,false,view));
    CHECK(!cpu.resolve_data(reused_base,true,view));
    process->delete_chunk(owned_exec);
    CHECK(!cpu.resolve_data(reused_base,true,view));
    process->delete_chunk(owned); std::free(owned_host);
    auto generation=mem::mapping_generation.load();
    auto *exec_alias=make(static_cast<prot>(prot_read|prot_exec),data->host_base());
    CHECK(mem::mapping_generation.load()>generation);
    CHECK(!cpu.resolve_data(base,false,view));
    CHECK(!cpu.resolve_data(base,true,view));
    process->delete_chunk(exec_alias);
    CHECK(!cpu.resolve_data(base,true,view)); // Retired executable backing remains conservative.
    process->delete_chunk(external);
    const auto old=data->base(process.get()); process->delete_chunk(data);
    CHECK(!cpu.resolve_data(old,false,view));
    process->delete_chunk(ro); process->delete_chunk(code);
}
extern "C" EMSCRIPTEN_KEEPALIVE unsigned memory_controls(unsigned model) {
    if(!already_setup)log::setup_log(nullptr);
    arm::aot::memory_experiment::mode=0;
    model_controls(model ? mem::mem_model_type::flexible : mem::mem_model_type::multiple);
    // Exhaustion/invalid ranges never discard earlier authority or wrap into a grant.
    for(unsigned i=0;i<65;++i)mem::record_external_executable(reinterpret_cast<void*>(0x1000u+i*0x2000u),4096);
    CHECK(mem::external_executable_overlap(reinterpret_cast<void*>(0x70000000u),4096));
    mem::record_external_executable(reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()-1),8);
    CHECK(mem::external_executable_overlap(reinterpret_cast<void*>(0x60000000u),4096));
    std::printf("PASS real MMU authority %u controls\n",checks);
    return checks;
}
