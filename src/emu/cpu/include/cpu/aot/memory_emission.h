#pragma once

#include <cpu/aot/memory_experiment.h>
#include <cpu/aot/state_locals.h>
#include <cpu/aot/memory_access_profile.h>

namespace eka2l1::arm::aot {
    template<class W> void profile_memory_access(W &w, unsigned address, bool write, unsigned offset = 0) {
#if defined(EKA2L1_MEMORY_ACCESS_PROFILE) && defined(__EMSCRIPTEN__)
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        w.i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&memory_access_profile::active_row)));
        w.op(op_i32_load);imm(2);imm(0);
        w.get_local(address);
        if (offset) { w.i32_const(offset); w.op(op_i32_add); }
        w.i32_const(memory_access_profile::bucket_shift);w.op(op_i32_shr_u);
        w.i32_const(4);w.op(op_i32_shl);w.op(op_i32_add);
        w.tee_local(W::ENTRY);w.get_local(W::ENTRY);
        w.op(op_i64_load);imm(3);imm(write ? 8 : 0);
        w.op(op_i64_const);w.b.push_back(1);w.op(op_i64_add);
        w.op(op_i64_store);imm(3);imm(write ? 8 : 0);
#endif
    }
    // Three derived locals: address bias, arena start limit and valid view.
    // A zero limit makes the unsigned range test fail for every guest address;
    // a zero view routes invalid/endian accesses through the ordinary helper.
    template<class W> void direct_memory_setup(W &w) {
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        auto load=[&](unsigned off){w.op(op_i32_load);imm(2);imm(off);};
        w.i32_const(0);w.set_local(W::M+1);
        w.i32_const(0);w.set_local(W::M+2);
        w.load_i32(state_offsets::AOT_TLB);w.op(op_if);w.op(type_void);
        w.load_i32(state_offsets::CPSR);w.i32_const(0x200);w.op(op_i32_and);w.op(op_i32_eqz);
        w.op(op_if);w.op(type_void);
        w.load_i32(state_offsets::AOT_TLB);w.set_local(W::M+2);
        w.get_local(W::M+2);load(offsetof(memory_experiment::direct_view,bias));w.set_local(W::M);
        w.get_local(W::M+2);load(offsetof(memory_experiment::direct_view,arena_start_limit));w.set_local(W::M+1);
        w.op(op_end);w.op(op_end);
    }

    template<class W> void finish_memory_locals(W &w) {
        if (!w.direct_memory_used) return;
        auto body=std::move(w.b);w.b.clear();
        auto *marks = w.source.marks; w.source.marks = nullptr;
        direct_memory_setup(w);
        w.cache.reload_suffix=std::move(w.b);w.b=std::move(body);
        w.source.marks = marks;
        if (!w.cache.enabled) {
            if (marks && !marks->empty()) {
                for (auto &mark : *marks) mark.offset += w.cache.reload_suffix.size();
                marks->insert(marks->begin(), {0, 0, source_kind::memory_check});
            }
            w.b.insert(w.b.begin(),w.cache.reload_suffix.begin(),w.cache.reload_suffix.end());
        }
    }

    template<class W> void arena_fits(W &w,unsigned address,unsigned bytes) {
        if (!bytes || bytes > memory_experiment::direct_size) { w.i32_const(0);return; }
        w.get_local(address);w.i32_const(memory_experiment::direct_begin);w.op(op_i32_sub);
        if (bytes > 64) {
            // Larger block spans retain an explicit whole-span proof.
            w.op(op_i64_extend_i32_u);w.i32_const(bytes-64);w.op(op_i64_extend_i32_u);w.op(op_i64_add);
            w.get_local(W::M+1);w.op(op_i64_extend_i32_u);w.op(op_i64_lt_u);
        } else { w.get_local(W::M+1);w.op(op_i32_lt_u); }
    }

    template<class W> void direct_host(W &w,unsigned address,unsigned bytes,bool write,unsigned alignment) {
        source_scope provenance(w.source, source_kind::memory_check);
        w.direct_memory_used=true;
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        auto load=[&](unsigned off){w.op(op_i32_load);imm(2);imm(off);};
        // Return the pointer through the WASM value stack. Successful paths
        // avoid initializing HOST before replacing it; misses still produce zero.
        if (alignment>1) {
            w.get_local(address);w.i32_const(alignment-1);w.op(op_i32_and);w.op(op_i32_eqz);
            w.op(op_if);w.op(type_i32);
        }
        arena_fits(w,address,bytes);
        w.op(op_if);w.op(type_i32);
        w.get_local(address);w.get_local(W::M);w.op(op_i32_add);
        w.op(op_else);
        w.get_local(W::M+2);w.op(op_if);w.op(type_i32);
        const bool crossing=bytes>alignment || alignment>4096 || 4096%alignment;
        if (crossing) {
            w.get_local(address);w.i32_const(4095);w.op(op_i32_and);w.i32_const(4096-bytes);w.op(op_i32_le_u);
            w.op(op_if);w.op(type_i32);
        }
        // The fallback table pointer is fetched only outside the arena.
        w.get_local(W::M+2);load(offsetof(memory_experiment::direct_view,pages));
        w.get_local(address);w.i32_const(12);w.op(op_i32_shr_u);w.i32_const(3);w.op(op_i32_shl);w.op(op_i32_add);
        load(write?4:0);w.tee_local(W::HOST);
        w.op(op_if);w.op(type_i32);
        w.get_local(W::HOST);w.get_local(address);w.i32_const(4095);w.op(op_i32_and);w.op(op_i32_add);
        w.op(op_else);w.i32_const(0);
        w.op(op_end);
        if (crossing) {w.op(op_else);w.i32_const(0);w.op(op_end);}
        w.op(op_else);w.i32_const(0);
        w.op(op_end);w.op(op_end);
        if (alignment>1) {w.op(op_else);w.i32_const(0);w.op(op_end);}
        w.set_local(W::HOST);
    }

    template<class W> void guest_memory_op(W &w, std::uint8_t opcode, unsigned alignment, unsigned offset=0) {
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        w.op(opcode);imm(alignment);imm(offset);
    }

    // Consume a successful scalar lookup immediately, rather than publishing
    // HOST and testing it again. Failed lookups converge on one unchanged slow
    // path. Both success and failure remove executed operations in ARM/Thumb.
    template<class W, class Slow> void direct_access(W &w, unsigned bytes, bool write, Slow slow) {
        source_scope provenance(w.source, source_kind::memory_check);
        w.direct_memory_used=true;
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        auto access=[&] {
            source_scope access_provenance(w.source, source_kind::guest_memory);
            profile_memory_access(w, W::ADDRESS, write);
            if(write) w.get_local(W::VALUE);
            guest_memory_op(w,write ? (bytes==4?op_i32_store:bytes==2?op_i32_store16:op_i32_store8)
                : (bytes==4?op_i32_load:bytes==2?op_i32_load16_u:op_i32_load8_u),bytes==4?2:bytes==2?1:0);
        };
        w.op(op_block);w.op(write?type_void:type_i32);
        w.op(op_block);w.op(type_void);
        arena_fits(w,W::ADDRESS,bytes);
        w.op(op_if);w.op(type_void);
        w.get_local(W::ADDRESS);w.get_local(W::M);w.op(op_i32_add);access();
        w.op(op_br);imm(2);
        w.op(op_end);
        w.get_local(W::M+2);w.op(op_i32_eqz);w.op(op_br_if);imm(0);
        if(bytes>1) {
            w.get_local(W::ADDRESS);w.i32_const(4095);w.op(op_i32_and);w.i32_const(4096-bytes);w.op(op_i32_gt_u);
            w.op(op_br_if);imm(0);
        }
        w.get_local(W::M+2);guest_memory_op(w,op_i32_load,2,offsetof(memory_experiment::direct_view,pages));
        w.get_local(W::ADDRESS);w.i32_const(12);w.op(op_i32_shr_u);w.i32_const(3);w.op(op_i32_shl);w.op(op_i32_add);
        guest_memory_op(w,op_i32_load,2,write?4:0);w.tee_local(W::HOST);
        w.op(op_i32_eqz);w.op(op_br_if);imm(0);
        w.get_local(W::HOST);w.get_local(W::ADDRESS);w.i32_const(4095);w.op(op_i32_and);w.op(op_i32_add);access();
        w.op(op_br);imm(1);
        w.op(op_end);
        slow();
        w.op(op_end);
    }
}
