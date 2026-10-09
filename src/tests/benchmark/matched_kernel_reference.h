// Restricted, compile-time-unrolled reference for offline compiler comparisons.
// Uses production memory helpers on exceptional accesses; never installed in a game.
#pragma once
#include <cpu/dyncom/arm_dyncom.h>
#include <array>
#include <cstring>
#include <cstdint>
namespace eka2l1::arm {
struct matched_kernel_access { static ARMul_State *state(dyncom_core &c) { return c.state_.get(); } };
}
namespace matched {
using u32=std::uint32_t;
inline u32 reference_budget=1; // Offline native oracle, never guest CPU state.
struct Frame {
    ARMul_State *s;
    std::array<u32,16> r;
    u32 n,z,c,v,t,cpsr,budget,count=0;
    u32 page[2]={0,0}; unsigned char *base[2]={nullptr,nullptr};
    std::uintptr_t code_begin,code_end;
    Frame(ARMul_State *s, std::uintptr_t begin,std::uintptr_t end):s(s),code_begin(begin),code_end(end) { reload(); budget=reference_budget; }
    __attribute__((always_inline)) void reload() { std::memcpy(r.data(),s->Reg.data(),64);n=s->NFlag;z=s->ZFlag;c=s->CFlag;v=s->VFlag;t=s->TFlag;cpsr=s->Cpsr; }
    __attribute__((always_inline)) void flush() { std::memcpy(s->Reg.data(),r.data(),64);s->NFlag=n;s->ZFlag=z;s->CFlag=c;s->VFlag=v;s->TFlag=t;s->Cpsr=cpsr; }
    __attribute__((always_inline)) unsigned char *host(u32 a,unsigned bytes,bool write) {
        const unsigned w=write;
        if(base[w] && (a & (0xfffff000u|(bytes-1)))==page[w])return base[w]+(a&4095);
        if(!s->mem_cache_ || a<4096 || (a&(bytes-1)) || (cpsr&0x200))return nullptr;
        const auto &e=s->mem_cache_->entries[(a>>12)&511];
        if((write?e.write_addr:e.read_addr)!=(a&~4095u) || !e.host_base)return nullptr;
        page[w]=a&~4095u;base[w]=e.host_base;return e.host_base+(a&4095);
    }
    __attribute__((always_inline)) unsigned char *span(u32 a,unsigned bytes,bool write) {
        if((a&3)||(a&4095)>4096-bytes)return nullptr;
        return host(a,4,write);
    }
    __attribute__((always_inline)) void changed(unsigned char *p,unsigned bytes) {
        auto a=reinterpret_cast<std::uintptr_t>(p);
        if(a<code_end && a+bytes>code_begin)s->aot_exit=1;
    }
    template<unsigned bytes,bool write> __attribute__((always_inline)) inline u32 access(u32 a,u32 value=0) {
        if(auto p=host(a,bytes,write)) {
            if constexpr(write) { std::memcpy(p,&value,bytes);changed(p,bytes);return 0; }
            else { u32 v=0;std::memcpy(&v,p,bytes);return v; }
        }
        flush();
        if constexpr(write) {
            if constexpr(bytes==4)s->WriteMemory32(a,value);
            if constexpr(bytes==2)s->WriteMemory16(a,static_cast<std::uint16_t>(value));
            if constexpr(bytes==1)s->WriteMemory8(a,static_cast<std::uint8_t>(value));
        } else {
            if constexpr(bytes==4)value=s->ReadMemory32(a);
            if constexpr(bytes==2)value=s->ReadMemory16(a);
            if constexpr(bytes==1)value=s->ReadMemory8(a);
        }
        reload();s->aot_exit=1;base[0]=base[1]=nullptr;return value;
    }
    template<u32 op> __attribute__((always_inline)) bool instruction() {
        static_assert((op>>28)==14,"only unconditional fixtures");
        if(count>=budget || s->aot_exit)return false;
        ++count; const u32 pc=r[15]; bool terminal=false;
        if constexpr((op&0x0f8000f0)==0x00800090) {
            constexpr auto lo=(op>>12)&15,hi=(op>>16)&15;
            const std::uint64_t product=(op&(1u<<22))?
                std::uint64_t(std::int64_t(std::int32_t(r[op&15]))*std::int64_t(std::int32_t(r[(op>>8)&15]))):
                std::uint64_t(r[op&15])*r[(op>>8)&15];
            const auto sum=product+((op&(1u<<21))?((std::uint64_t(r[hi])<<32)|r[lo]):0);
            r[lo]=u32(sum);r[hi]=u32(sum>>32);
            if constexpr(op&(1u<<20)){n=r[hi]>>31;z=sum==0;}
        } else if constexpr(((op>>25)&7)==4) {
            constexpr bool load=op&(1u<<20),wb=op&(1u<<21),up=op&(1u<<23),pre=op&(1u<<24);
            constexpr unsigned rn=(op>>16)&15,mask=op&65535,words=__builtin_popcount(mask);
            u32 a=r[rn]+(up?(pre?4:0):u32(-int(words*4)+(pre?0:4)));
            auto p=span(a,words*4,!load);unsigned off=0;
            for(unsigned k=0;k<16;++k)if(mask&(1u<<k)) {
                if constexpr(load) { if(p)std::memcpy(&r[k],p+off,4);else r[k]=access<4,false>(a+off); }
                else { u32 value=k==15?pc+8:r[k];if(p)std::memcpy(p+off,&value,4);else access<4,true>(a+off,value); }
                off+=4;
            }
            if constexpr(!load)if(p)changed(p,words*4);
            if constexpr(wb)r[rn]+=up?words*4:u32(-int(words*4));
            if constexpr(load && (mask&0x8000)){t=r[15]&1;terminal=true;}
        } else if constexpr(((op>>26)&3)==1) {
            static_assert(!(op&(1u<<25)),"immediate memory offsets only");
            constexpr bool load=op&(1u<<20),byte=op&(1u<<22),up=op&(1u<<23),pre=op&(1u<<24),wb=op&(1u<<21);
            constexpr unsigned rn=(op>>16)&15,rd=(op>>12)&15;
            const u32 adjusted=r[rn]+(up?(op&4095):u32(-int(op&4095)));
            const u32 a=pre?adjusted:r[rn];
            if constexpr(load)r[rd]=access<byte?1:4,false>(a);
            else access<byte?1:4,true>(a,rd==15?pc+8:r[rd]);
            if constexpr(!pre||wb)r[rn]=adjusted;
            if constexpr(load&&rd==15){t=r[15]&1;terminal=true;}
        } else if constexpr((op&0x0e000090)==0x00000090) {
            static_assert(((op>>5)&3)==1,"halfword only");
            constexpr bool load=op&(1u<<20),up=op&(1u<<23),pre=op&(1u<<24),wb=op&(1u<<21);
            constexpr unsigned rn=(op>>16)&15,rd=(op>>12)&15;
            const u32 offset=(op&(1u<<22))?((op&15)|((op>>4)&240)):r[op&15];
            const u32 adjusted=r[rn]+(up?offset:-offset),a=pre?adjusted:r[rn];
            if constexpr(load)r[rd]=access<2,false>(a);else access<2,true>(a,r[rd]);
            if constexpr(!pre||wb)r[rn]=adjusted;
        } else {
            static_assert(((op>>26)&3)==0,"data processing only");
            constexpr unsigned kind=(op>>21)&15,rn=(op>>16)&15,rd=(op>>12)&15;
            u32 rhs;
            if constexpr(op&(1u<<25)) { constexpr unsigned shift=((op>>8)&15)*2,imm=op&255;if constexpr(shift)rhs=(imm>>shift)|(imm<<(32-shift));else rhs=imm; }
            else { static_assert(!(op&16),"immediate shifts only");constexpr unsigned shift=(op>>7)&31,type=(op>>5)&3;rhs=r[op&15];if constexpr(type==0)rhs<<=shift;else if constexpr(type==1)rhs=shift?rhs>>shift:0;else if constexpr(type==2)rhs=u32(std::int32_t(rhs)>>(shift?shift:31));else static_assert(type!=3,"ROR not used"); }
            if constexpr(kind==13)r[rd]=rhs;
            else if constexpr(kind==4)r[rd]=r[rn]+rhs;
            else if constexpr(kind==12)r[rd]=r[rn]|rhs;
            else static_assert(kind==13||kind==4||kind==12,"unsupported opcode");
            if constexpr(op&(1u<<20)){static_assert(kind==13,"MOVS only");n=r[rd]>>31;z=r[rd]==0;}
        }
        if(!terminal)r[15]=pc+4;
        return !terminal && !s->aot_exit;
    }
};
// Generated template instantiations live in a local build directory, not Git.
}
