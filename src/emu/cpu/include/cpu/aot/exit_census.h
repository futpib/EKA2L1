#pragma once
#include <common/guest_profile.h>
#include <common/performance.h>
#include <cstdint>
#include <map>
#include <tuple>

namespace eka2l1::arm::aot::exit_census {
    // Diagnostic-only, configured before the guest starts. The guest worker
    // owns these fields, just as it owns the existing instruction census.
    inline bool enabled = false;
    enum reason : unsigned { unknown, control, guard, unsupported, memory,
        source_end, interrupt, status, emission_end };
    inline std::uint32_t last_reason=0, last_pc=0, last_opcode=0, effects=0, last_constraint=0;
    // These identify the first rejecting validator filter, not a full ARM decode.
    // Raw opcode/PC accompany counts so special encodings can be audited correctly.
    enum leaf_restriction : unsigned { no_restriction, predicates_disabled, reserved_predicate,
        conditional_memory, conditional_transfer, nested_call, internal_branch, block_transfer,
        coprocessor_or_supervisor, sp_operand, lr_operand, pc_operand, register_memory_shift,
        status_or_misc, sp_index, lr_index, pc_index, sp_shift, lr_shift, pc_shift,
        multiply, halfword_or_signed_transfer, swap_or_exclusive, other_extra_transfer };
    inline const char *restriction_name(unsigned value) {
        static const char *names[]={"unrecorded","predicates_disabled","reserved_predicate",
            "conditional_memory","conditional_transfer","nested_call","internal_branch","block_transfer",
            "coprocessor_or_supervisor","sp_rn_or_rd_field","lr_rn_or_rd_field","pc_rn_or_rd_field","register_memory_shift",
            "status_or_misc","sp_rm_field","lr_rm_field","pc_rm_field","sp_rs_field","lr_rs_field","pc_rs_field",
            "multiply","halfword_or_signed_transfer","swap_or_exclusive","other_extra_transfer"};
        return value<sizeof(names)/sizeof(names[0])?names[value]:"unrecorded";
    }
    struct leaf_refusal { unsigned constraint=0, detail=0; std::uint32_t pc=0, opcode=0; };
    inline std::uint32_t last_restriction=0,last_rejected_pc=0,last_rejected_opcode=0;
    inline std::uint32_t last_guard_host=0,last_guard_size=0,guard_hits=0;
    inline std::map<std::string,std::uint64_t> exits, runners, compilation, invalidations, call_constraints,
        call_restrictions, code_guard_outcomes;
    using rejected_key=std::tuple<std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,unsigned>;
    inline std::map<rejected_key,std::uint64_t> rejected_sites;
    inline std::uint64_t dropped_rejected_calls=0;
    inline std::uint64_t validated_entries=0,validated_primary_bytes=0,validated_dependency_spans=0,validated_dependency_bytes=0,protected_interval_bytes=0;
    using edge_key=std::tuple<std::string,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t>;
    inline std::map<edge_key,std::uint64_t> edges;
    using site_key=std::tuple<std::uint32_t,std::uint32_t,std::string>;
    inline std::map<site_key,std::uint64_t> sites;
    inline std::map<std::pair<std::uint32_t,std::string>,std::uint64_t> probes;
    inline void probe(std::uint32_t pc,const std::uint8_t *bytes,std::size_t size) {
        if(!enabled || probes.size()>=8192)return;
        const char *digits="0123456789abcdef";std::string hex;
        for(std::size_t n=0;n<size;++n){hex+=digits[bytes[n]>>4];hex+=digits[bytes[n]&15];}
        ++probes[{pc,hex}];
    }
    inline std::uint64_t calls=0, dropped_edges=0, dropped_sites=0, zero=0;
    inline bool counting() { return enabled && common::performance::counting(); }
    inline void compile_site(std::uint32_t pc,std::uint32_t op,const char *why) {
        if(!enabled)return;
        ++compilation[why];
        site_key k{pc,op,why};auto it=sites.find(k);
        if(it!=sites.end())++it->second;else if(sites.size()<131072)sites.emplace(k,1);else ++dropped_sites;
    }
    inline const char *classify(bool thumb, unsigned returned, unsigned budget, unsigned exit_flag) {
        if(exit_flag && effects==3)return "helper_and_code_write";
        if(exit_flag && (effects&2))return "code_write_guard";
        if(exit_flag && (effects&1))return "helper_exit";
        if(returned>=budget)return "budget";
        if(last_reason==guard)return returned>=budget?"budget":"exit_flag";
        if(last_reason==unsupported)return "unsupported";
        if(last_reason==memory)return "memory_guard";
        if(last_reason==source_end)return "source_window_end";
        if(last_reason==emission_end)return "emission_end";
        if(last_reason==interrupt)return "interrupt";
        if(last_reason==status)return "status_mode";
        if(last_reason!=control)return "unclassified";
        const auto op=last_opcode;
        if(!thumb) {
            if(((op>>25)&7)==5)return op&(1u<<24)?"call":"external_direct_branch";
            if((op&0x0ffffff0u)==0x012fff10u)return (op&15)==14?"return_bx_lr":"indirect_bx";
            if((op&0x0ffffff0u)==0x012fff30u)return "indirect_call";
            if((op&0x0fffffffu)==0x01a0f00eu)return "return_mov_pc_lr";
            if(((op>>25)&7)==4 && (op&32768))return "indirect_ldm_pc";
            if(((op>>26)&3)==1 && ((op>>12)&15)==15)return "indirect_load_pc";
            return "other_control";
        }
        if((op&0xf800)==0xf000 || (op&0xf800)==0xf800 || (op&0xf800)==0xe800)return "thumb_call_half";
        if((op&0xff87)==0x4700)return ((op>>3)&15)==14?"return_bx_lr":"indirect_bx";
        if((op&0xff00)==0xbd00)return "return_pop_pc";
        if((op&0xf000)==0xd000 || (op&0xf800)==0xe000)return "external_direct_branch";
        return "other_thumb_control";
    }
    inline void record(std::uint32_t entry,std::uint32_t to,std::uint32_t asid,unsigned count,unsigned budget,unsigned flag) {
        if(!counting())return;
        ++calls;if(!count)++zero;
        const auto name=classify(entry&1,count,budget,flag);++exits[name];
        if(std::string(name)=="call") {
            const char *names[]={"unrecorded","inline_site_limit","leaf_instruction_limit","callee_unsupported",
                "callee_mapping_extent","callee_unmapped_or_other_space","no_leaf_resolver","conditional_call"};
            ++call_constraints[names[last_constraint<8?last_constraint:0]];
            if(last_constraint==3) {
                ++call_restrictions[restriction_name(last_restriction)];
                rejected_key key{asid,last_pc,last_rejected_pc,last_rejected_opcode,last_restriction};
                auto found=rejected_sites.find(key);
                if(found!=rejected_sites.end())++found->second;
                else if(rejected_sites.size()<131072)rejected_sites.emplace(key,1);
                else ++dropped_rejected_calls;
            }
        }
        if(calls%common::guest_profile::state.stride)return;
        edge_key k{name,entry,to,asid,last_pc,last_opcode};auto it=edges.find(k);
        if(it!=edges.end())++it->second;else if(edges.size()<131072)edges.emplace(k,1);else ++dropped_edges;
    }
    inline std::string report() {
        std::ostringstream o;o<<"{\"calls\":"<<calls<<",\"zero_progress\":"<<zero<<",\"stride\":"<<common::guest_profile::state.stride
            <<",\"dropped_edges\":"<<dropped_edges<<",\"dropped_compile_sites\":"<<dropped_sites;
        const auto counts=[&](const char *name,const auto &values){o<<",\""<<name<<"\":{";bool first=true;for(const auto &[key,n]:values){if(!first)o<<',';first=false;o<<common::guest_profile::quote(key)<<':'<<n;}o<<'}';};
        o<<",\"validated_snapshot_requests\":{\"entries\":"<<validated_entries<<",\"primary_bytes\":"<<validated_primary_bytes
            <<",\"dependency_spans\":"<<validated_dependency_spans<<",\"dependency_bytes\":"<<validated_dependency_bytes<<",\"protected_interval_bytes\":"<<protected_interval_bytes<<'}';
        counts("callee_restrictions",call_restrictions);counts("code_guard_outcomes",code_guard_outcomes);
        counts("region_exits",exits);counts("runner_returns",runners);counts("invalidations",invalidations);counts("call_constraints",call_constraints);counts("lifetime_compile_events",compilation);
        o<<",\"sampled_edges\":[";bool first=true;for(const auto &[k,n]:edges){if(!first)o<<',';first=false;const auto &[why,from,to,asid,site,op]=k;
            o<<"{\"reason\":"<<common::guest_profile::quote(why)<<",\"from\":"<<from<<",\"to\":"<<to<<",\"asid\":"<<asid<<",\"site\":"<<site<<",\"opcode\":"<<op<<",\"samples\":"<<n<<'}';}
        o<<"],\"dropped_rejected_calls\":"<<dropped_rejected_calls<<",\"rejected_call_sites\":[";first=true;
        for(const auto &[key,n]:rejected_sites){if(!first)o<<',';first=false;const auto &[space,caller,pc,opcode,detail]=key;
            o<<"{\"address_space\":"<<space<<",\"caller_pc\":"<<caller<<",\"rejected_pc\":"<<pc<<",\"opcode\":"<<opcode
             <<",\"restriction\":"<<common::guest_profile::quote(restriction_name(detail))<<",\"calls\":"<<n<<'}';}
        o<<"],\"compile_sites\":[";first=true;for(const auto &[k,n]:sites){if(!first)o<<',';first=false;const auto &[pc,op,why]=k;
            o<<"{\"pc\":"<<pc<<",\"opcode\":"<<op<<",\"reason\":"<<common::guest_profile::quote(why)<<",\"translations\":"<<n<<'}';}o<<"],\"leaf_probes\":[";first=true;for(const auto &[key,n]:probes){if(!first)o<<',';first=false;
            o<<"{\"address\":"<<key.first<<",\"bytes\":"<<common::guest_profile::quote(key.second)<<",\"probes\":"<<n<<'}';}o<<"]}";return o.str();
    }
}
