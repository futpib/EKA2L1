#pragma once
#include <common/guest_profile.h>
#include <common/performance.h>
#include <cstdint>
#include <map>
#include <tuple>

namespace eka2l1::arm::aot::exit_census {
    // Diagnostic-only, configured before the guest starts. The guest worker
    // owns the compilation and memory/fault diagnostic fields below.
    inline common::diagnostics::flag enabled = false;
    enum reason : unsigned { unknown, control, guard, unsupported, memory,
        source_end, interrupt, status, emission_end };
    inline std::uint32_t last_reason=0, last_pc=0, last_opcode=0, effects=0, last_constraint=0;
    // These identify the first rejecting validator filter, not a full ARM decode.
    // Raw opcode/PC accompany counts so special encodings can be audited correctly.
    enum leaf_restriction : unsigned { no_restriction, predicates_disabled, reserved_predicate,
        conditional_memory, conditional_transfer, nested_call, internal_branch, block_transfer,
        coprocessor_or_supervisor, sp_operand, lr_operand, pc_operand, register_memory_shift,
        status_or_misc, sp_index, lr_index, pc_index, sp_shift, lr_shift, pc_shift,
        multiply, halfword_or_signed_transfer, swap_or_exclusive, other_extra_transfer, forward_target_after_return };
    inline const char *restriction_name(unsigned value) {
        static const char *names[]={"unrecorded","predicates_disabled","reserved_predicate",
            "conditional_memory","conditional_transfer","nested_call","internal_branch","block_transfer",
            "coprocessor_or_supervisor","sp_rn_or_rd_field","lr_rn_or_rd_field","pc_rn_or_rd_field","register_memory_shift",
            "status_or_misc","sp_rm_field","lr_rm_field","pc_rm_field","sp_rs_field","lr_rs_field","pc_rs_field",
            "multiply","halfword_or_signed_transfer","swap_or_exclusive","other_extra_transfer","forward_target_after_return"};
        return value<sizeof(names)/sizeof(names[0])?names[value]:"unrecorded";
    }
    struct leaf_refusal { unsigned constraint=0, detail=0; std::uint32_t pc=0, opcode=0; };
    inline std::uint32_t last_restriction=0,last_rejected_pc=0,last_rejected_opcode=0;
    inline std::uint32_t last_guard_host=0,last_guard_size=0,guard_hits=0;
    // Entry proof failures call a precise fallback before any guest effect;
    // they are distinct from a post-store code-write exit. Retain each bounded
    // overlap so interval gaps can be classified against actual snapshots.
    inline std::uint32_t entry_proof_failed=0,entry_overlap_count=0,entry_other_failure=0;
    inline std::uint32_t entry_proof_attempted=0,entry_read_spans=0,entry_write_spans=0;
    struct entry_overlap { std::uint32_t host=0,bytes=0; };
    inline entry_overlap entry_overlaps[32];
    inline std::uint64_t entry_proof_fallbacks=0,entry_overlap_dropped=0;
    inline std::uint64_t entry_proof_attempts=0,entry_read_span_checks=0,entry_write_span_checks=0;
    inline std::map<std::string,std::uint64_t> entry_proof_outcomes,entry_fallback_causes;
    inline std::map<std::string,std::uint64_t> compilation, invalidations, code_guard_outcomes;
    inline std::uint64_t validated_entries=0,validated_primary_bytes=0,validated_dependency_spans=0,validated_dependency_bytes=0,protected_interval_bytes=0;
    using site_key=std::tuple<std::uint32_t,std::uint32_t,std::string>;
    inline std::map<site_key,std::uint64_t> sites;
    inline std::map<std::pair<std::uint32_t,std::string>,std::uint64_t> probes;
    inline void probe(std::uint32_t pc,const std::uint8_t *bytes,std::size_t size) {
        if(!enabled || probes.size()>=8192)return;
        const char *digits="0123456789abcdef";std::string hex;
        for(std::size_t n=0;n<size;++n){hex+=digits[bytes[n]>>4];hex+=digits[bytes[n]&15];}
        ++probes[{pc,hex}];
    }
    inline std::uint64_t dropped_sites=0;
    inline bool counting() { return enabled && common::performance::counting(); }
    inline void compile_site(std::uint32_t pc,std::uint32_t op,const char *why) {
        if(!enabled)return;
        ++compilation[why];
        site_key k{pc,op,why};auto it=sites.find(k);
        if(it!=sites.end())++it->second;else if(sites.size()<131072)sites.emplace(k,1);else ++dropped_sites;
    }
    inline std::string report() {
        std::ostringstream o;o<<"{\"dropped_compile_sites\":"<<dropped_sites;
        const auto counts=[&](const char *name,const auto &values){o<<",\""<<name<<"\":{";bool first=true;for(const auto &[key,n]:values){if(!first)o<<',';first=false;o<<common::guest_profile::quote(key)<<':'<<n;}o<<'}';};
        o<<",\"validated_snapshot_requests\":{\"entries\":"<<validated_entries<<",\"primary_bytes\":"<<validated_primary_bytes
            <<",\"dependency_spans\":"<<validated_dependency_spans<<",\"dependency_bytes\":"<<validated_dependency_bytes<<",\"protected_interval_bytes\":"<<protected_interval_bytes<<'}';
        counts("code_guard_outcomes",code_guard_outcomes);
        counts("entry_proof_overlap_outcomes",entry_proof_outcomes);
        counts("entry_proof_fallback_causes",entry_fallback_causes);
        o<<",\"entry_proof_fallbacks\":"<<entry_proof_fallbacks<<",\"entry_proof_overlap_dropped\":"<<entry_overlap_dropped;
        o<<",\"entry_proof_attempts\":"<<entry_proof_attempts<<",\"entry_read_span_checks\":"<<entry_read_span_checks<<",\"entry_write_span_checks\":"<<entry_write_span_checks;
        counts("invalidations",invalidations);counts("lifetime_compile_events",compilation);
        o<<",\"compile_sites\":[";bool first=true;for(const auto &[k,n]:sites){if(!first)o<<',';first=false;const auto &[pc,op,why]=k;
            o<<"{\"pc\":"<<pc<<",\"opcode\":"<<op<<",\"reason\":"<<common::guest_profile::quote(why)<<",\"translations\":"<<n<<'}';}o<<"],\"leaf_probes\":[";first=true;for(const auto &[key,n]:probes){if(!first)o<<',';first=false;
            o<<"{\"address\":"<<key.first<<",\"bytes\":"<<common::guest_profile::quote(key.second)<<",\"probes\":"<<n<<'}';}o<<"]}";return o.str();
    }
}
