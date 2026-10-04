#pragma once

#include <cpu/aot/memory_experiment.h>
#include <cpu/aot/state_locals.h>

namespace eka2l1::arm::aot {
    // Three derived locals: address bias, enabled-arena mask and valid view.
    // A zero mask makes the unsigned range test fail for every guest address;
    // a zero view routes invalid/endian accesses through the ordinary helper.
    template<class W> void compact_memory_setup(W &w) {
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        auto load=[&](unsigned off){w.op(op_i32_load);imm(2);imm(off);};
        w.i32_const(0);w.set_local(W::M+1);
        w.i32_const(0);w.set_local(W::M+2);
        w.load_i32(state_offsets::AOT_TLB);w.op(op_if);w.op(type_void);
        w.load_i32(state_offsets::CPSR);w.i32_const(0x200);w.op(op_i32_and);w.op(op_i32_eqz);
        w.op(op_if);w.op(type_void);
        w.load_i32(state_offsets::AOT_TLB);w.set_local(W::M+2);
        w.get_local(W::M+2);load(offsetof(memory_experiment::direct_view,bias));w.set_local(W::M);
        w.get_local(W::M+2);load(offsetof(memory_experiment::direct_view,arena_mask));w.set_local(W::M+1);
        w.op(op_end);w.op(op_end);
    }

    template<class W> void finish_memory_locals(W &w) {
        if (!w.compact_memory_used) return;
        auto body=std::move(w.b);w.b.clear();
        compact_memory_setup(w);
        w.cache.reload_suffix=std::move(w.b);w.b=std::move(body);
        if (!w.cache.enabled)
            w.b.insert(w.b.begin(),w.cache.reload_suffix.begin(),w.cache.reload_suffix.end());
    }

    template<class W> void compact_host(W &w,unsigned address,unsigned bytes,bool write,unsigned alignment) {
        w.compact_memory_used=true;
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        auto load=[&](unsigned off){w.op(op_i32_load);imm(2);imm(off);};
        w.i32_const(0);w.set_local(W::HOST);
        if (alignment>1) {
            w.get_local(address);w.i32_const(alignment-1);w.op(op_i32_and);w.op(op_i32_eqz);
            w.op(op_if);w.op(type_void);
        }
        w.get_local(address);w.i32_const(memory_experiment::direct_begin);w.op(op_i32_sub);
        w.get_local(W::M+1);
        w.i32_const(bytes<=memory_experiment::direct_size ? memory_experiment::direct_size-bytes+1 : 0);
        w.op(op_i32_and);w.op(op_i32_lt_u);
        w.op(op_if);w.op(type_void);
        w.get_local(address);w.get_local(W::M);w.op(op_i32_add);w.set_local(W::HOST);
        w.op(op_else);
        w.get_local(W::M+2);w.op(op_if);w.op(type_void);
        const bool crossing=bytes>alignment || alignment>4096 || 4096%alignment;
        if (crossing) {
            w.get_local(address);w.i32_const(4095);w.op(op_i32_and);w.i32_const(4096-bytes);w.op(op_i32_le_u);
            w.op(op_if);w.op(type_void);
        }
        // The fallback table pointer is fetched only outside the arena.
        w.get_local(W::M+2);load(offsetof(memory_experiment::direct_view,pages));
        w.get_local(address);w.i32_const(12);w.op(op_i32_shr_u);w.i32_const(3);w.op(op_i32_shl);w.op(op_i32_add);
        load(write?4:0);w.tee_local(W::HOST);
        w.op(op_if);w.op(type_void);
        w.get_local(W::HOST);w.get_local(address);w.i32_const(4095);w.op(op_i32_and);w.op(op_i32_add);w.set_local(W::HOST);
        w.op(op_end);
        if (crossing) w.op(op_end);
        w.op(op_end);w.op(op_end);
        if (alignment>1) w.op(op_end);
    }

    // Shared ARM/Thumb experimental address lowering. M..M+3 are scratch
    // locals, or the last contiguous allocation's begin/size/backing/permissions.
    template<class W> void experimental_host(W &w, unsigned address, unsigned bytes, bool write, unsigned alignment) {
        if (memory_experiment::compact_direct()) { compact_host(w,address,bytes,write,alignment);return; }
        using S=state_offsets;
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        auto load=[&](unsigned offset){w.op(op_i32_load);imm(2);imm(offset);};
        auto matches=[&] {
            w.get_local(W::M+1);w.i32_const(bytes);w.op(op_i32_ge_u);
            w.get_local(address);w.get_local(W::M);w.op(op_i32_sub);
            w.get_local(W::M+1);w.i32_const(bytes);w.op(op_i32_sub);w.op(op_i32_le_u);w.op(op_i32_and);
            w.get_local(W::M+3);w.i32_const(write?2:1);w.op(op_i32_and);w.op(op_i32_eqz);w.op(op_i32_eqz);w.op(op_i32_and);
        };
        w.i32_const(0);w.set_local(W::HOST);
        w.load_i32(S::AOT_TLB);w.op(op_if);w.op(type_void);
        w.get_local(address);w.i32_const(alignment-1);w.op(op_i32_and);w.op(op_i32_eqz);
        w.load_i32(S::CPSR);w.i32_const(0x200);w.op(op_i32_and);w.op(op_i32_eqz);w.op(op_i32_and);
        w.op(op_if);w.op(type_void);
        if(memory_experiment::mode==1) {
            matches();w.op(op_i32_eqz);w.op(op_if);w.op(type_void);
            w.load_i32(S::AOT_TLB);w.get_local(address);w.i32_const(12);w.op(op_i32_shr_u);
            w.i32_const(4);w.op(op_i32_shl);w.op(op_i32_add);w.set_local(W::ENTRY);
            for(unsigned n=0;n<4;++n){w.get_local(W::ENTRY);load(n*4);w.set_local(W::M+n);}
            w.op(op_end);
            matches();w.op(op_if);w.op(type_void);
            w.get_local(W::M+2);w.get_local(address);w.get_local(W::M);w.op(op_i32_sub);w.op(op_i32_add);w.set_local(W::HOST);
            w.op(op_end);
        } else if(memory_experiment::mode==3) {
            w.get_local(address);w.i32_const(4095);w.op(op_i32_and);w.i32_const(4096-bytes);w.op(op_i32_le_u);
            w.op(op_if);w.op(type_void);
            w.load_i32(S::AOT_TLB);w.get_local(address);w.i32_const(12);w.op(op_i32_shr_u);
            w.i32_const(3);w.op(op_i32_shl);w.op(op_i32_add);load(write?4:0);w.tee_local(W::HOST);
            w.op(op_if);w.op(type_void);
            w.get_local(W::HOST);w.get_local(address);w.i32_const(4095);w.op(op_i32_and);w.op(op_i32_add);w.set_local(W::HOST);
            w.op(op_end);w.op(op_end);
        } else {
            // Cache the process arena for this region. Callbacks invalidate it.
            w.get_local(W::M+3);w.op(op_i32_eqz);w.op(op_if);w.op(type_void);
            for(unsigned n=0;n<4;++n){w.load_i32(S::AOT_TLB);load(n*4);w.set_local(W::M+n);}
            w.op(op_end);
            w.get_local(W::M+1);w.i32_const(bytes);w.op(op_i32_ge_u);
            w.get_local(address);w.get_local(W::M);w.op(op_i32_sub);
            w.get_local(W::M+1);w.i32_const(bytes);w.op(op_i32_sub);w.op(op_i32_le_u);w.op(op_i32_and);
            w.op(op_if);w.op(type_void);
            w.get_local(W::M+2);w.get_local(address);w.get_local(W::M);w.op(op_i32_sub);w.op(op_i32_add);w.set_local(W::HOST);
            w.op(op_else);
            w.get_local(address);w.i32_const(4095);w.op(op_i32_and);w.i32_const(4096-bytes);w.op(op_i32_le_u);
            w.op(op_if);w.op(type_void);
            w.get_local(W::M+3);w.get_local(address);w.i32_const(12);w.op(op_i32_shr_u);
            w.i32_const(3);w.op(op_i32_shl);w.op(op_i32_add);load(write?4:0);w.tee_local(W::HOST);
            w.op(op_if);w.op(type_void);
            w.get_local(W::HOST);w.get_local(address);w.i32_const(4095);w.op(op_i32_and);w.op(op_i32_add);w.set_local(W::HOST);
            w.op(op_end);w.op(op_end);w.op(op_end);
        }
        w.op(op_end);w.op(op_end);
    }

    template<class W> void guest_memory_op(W &w, std::uint8_t opcode, unsigned alignment, unsigned offset=0) {
        auto imm=[&](unsigned v){state_local_cache::leb(w.b,v);};
        w.op(opcode);imm(alignment);imm(offset);
    }
}
